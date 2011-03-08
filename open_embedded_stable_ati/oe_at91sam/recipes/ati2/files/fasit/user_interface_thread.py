USER_INTERFACE_PATH   = "/sys/class/target/user_interface/"

import os
import select
import logging

from QThread import *

#------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------     
class user_interface_thread(QThread):    
    UI_POLL_TIMEOUT_MS = 500
    def __init__(self, mover):
        QThread.__init__(self)
        self.logger = logging.getLogger('user_interface_thread')
        self.keep_going = True
        self.driver_path = USER_INTERFACE_PATH
        self.bit_button_path = self.driver_path + "bit_button"
        self.move_button_path = self.driver_path + "move_button"
        self.bit_button_fd = None
        self.move_button_fd = None
        self.is_moving = False
        self.is_mover = mover
        
    def check_driver(self):
        if (os.path.exists(self.driver_path) == False):
            self.logger.debug("sysfs path to driver does not exist (%s)", self.driver_path)
            raise ValueError('Path to user interface driver not found.')

        self.bit_button_fd = os.open(self.bit_button_path,os.O_RDONLY)
        if (self.is_mover):
            self.move_button_fd = os.open(self.move_button_path,os.O_RDONLY)

    def get_setting(self, setting_name):
        fd = os.open(self.driver_path + setting_name, os.O_RDONLY)
        setting = os.read(fd, 32)
        os.close(fd)
        return setting
        
    def set_setting(self, setting_name, setting):
        fd = os.open(self.driver_path + setting_name, os.O_RDWR)
        os.write(fd, setting)
        os.close(fd)
        return True
        
    def run(self):
        self.logger.debug('user interface thread running...')
        
        # clear any previous button presses on startup
        self.get_setting("bit_button")
        if (self.is_mover):
            self.get_setting("move_button")
        
        # process incoming commands            
        while self.keep_going:
            data = self.read_in()
            
            if(data is None):
                os.lseek(self.bit_button_fd,0,os.SEEK_SET)
                bit_button_status = os.read(self.bit_button_fd, 32)
                if (self.is_mover):
                    os.lseek(self.move_button_fd,0,os.SEEK_SET)
                    move_button_status = os.read(self.move_button_fd, 32)
                else:
                    move_button_status = "other"
                
                # these don't reset when read, so only do something if we need to change movement
                if (move_button_status == "stop\n" and self.is_moving == True):
                    self.logger.debug('move button stop 1')
                    self.write_out("stop")
                    os.close(self.move_button_fd)
                    self.is_moving = False
                elif (move_button_status == "forward\n" and self.is_moving == False):
                    self.logger.debug('move button forward 1')
                    self.write_out("forward")
                    os.close(self.move_button_fd)
                    self.is_moving = True
                elif (move_button_status == "reverse\n" and self.is_moving == False):
                    self.logger.debug('move button reverse 1')
                    self.write_out("reverse")
                    os.close(self.move_button_fd)
                    self.is_moving = True
                # this is reset when read
                elif (bit_button_status == "pressed\n"):
                    self.logger.debug('bit button pressed 1')
                    self.write_out("pressed")
                # poll to wait for change
                else:
                    p = select.poll()
                    os.lseek(self.bit_button_fd,0,os.SEEK_SET)
                    p.register(self.bit_button_fd, select.POLLERR|select.POLLPRI)
                    if (self.is_mover):
                        os.lseek(self.move_button_fd,0,os.SEEK_SET)
                        p.register(self.move_button_fd, select.POLLERR|select.POLLPRI)
                    s = p.poll(self.UI_POLL_TIMEOUT_MS)
                    if (self.is_mover):
                        os.close(self.move_button_fd)
                    
                    while (len(s) > 0):
                        fd, event = s.pop()
                        
                        if (fd == self.bit_button_fd):
                            self.logger.debug("Change in bit button status...")
                            bit_button_status = self.get_setting("bit_button")
                            if (bit_button_status == "pressed\n"):
                                self.logger.debug('bit button pressed 2')
                                self.write_out("pressed")
                        elif (self.is_mover and fd == self.move_button_fd):
                            self.logger.debug("Change in move button status...")
                            move_button_status = self.get_setting("move_button")
                            if (move_button_status == "forward\n"):
                                self.logger.debug('move button forward 2')
                                self.write_out("forward")
                                self.is_moving = True
                            elif (move_button_status == "reverse\n"):
                                self.logger.debug('move button reverse 2')
                                self.write_out("reverse")
                                self.is_moving = True
                            elif (move_button_status == "stop\n"):
                                if (self.is_moving):
                                    self.logger.debug('move button stop 2')
                                    self.write_out("stop")
                                self.is_moving = False
                                                     
                        else:
                            # TODO - report error
                            self.logger.debug("Error: unknown file descriptor")
                  
            elif data == "stop":
                self.keep_going = False  

        os.close(self.bit_button_fd)
        os.close(self.move_button_fd)
        self.logger.debug('...stopping')
           
