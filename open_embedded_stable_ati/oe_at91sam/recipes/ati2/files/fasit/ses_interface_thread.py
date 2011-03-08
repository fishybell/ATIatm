SES_INTERFACE_PATH   = "/sys/class/target/ses_interface/"

import os
import select
import logging

from QThread import *

#------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------     
class ses_interface_thread(QThread):    
    UI_POLL_TIMEOUT_MS = 5000
    def __init__(self):
        QThread.__init__(self)
        self.logger = logging.getLogger('ses_interface_thread')
        self.keep_going = True
        self.driver_path = SES_INTERFACE_PATH
        self.knob_path = self.driver_path + "knob"
        self.knob_fd = None
        
    def check_driver(self):
        if (os.path.exists(self.driver_path) == False):
            self.logger.debug("sysfs path to driver does not exist (%s)", self.driver_path)
            raise ValueError('Path to user interface driver not found.')

#        self.knob_fd = os.open(self.knob_path,os.O_RDONLY)

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
        
#    def run(self):
#        self.logger.debug('user interface thread running...')
#        
#        # clear any previous button presses on startup
#        oldvalue = self.get_setting("knob")[:-1] ;# clear out newline character
#        
#        # process incoming commands            
#        while self.keep_going:
#            data = self.read_in()
#            
#            if(data is None):
#                os.lseek(self.knob_fd,0,os.SEEK_SET)
#                knob_value = os.read(self.knob_fd, 32)[:-1] ;# clear out newline character
#                
#                if (knob_value != oldvalue):
#                    self.logger.debug("Change in knob value...")
#                    self.write_out(knob_value)
#                else:
#                    p = select.poll()
#                    os.lseek(self.knob_fd,0,os.SEEK_SET)
#                    p.register(self.knob_fd, select.POLLERR|select.POLLPRI)
#                    s = p.poll(self.UI_POLL_TIMEOUT_MS)
#                    
#                    while (len(s) > 0):
#                        fd, event = s.pop()
#                        
#                        if (fd == self.knob_fd):
#                            self.logger.debug("Change in knob value...")
#                            knob_value = os.read(self.knob_fd, 32)[:-1] ;# clear out newline character
#                            self.write_out(knob_value)
#                                                     
#                        else:
#                            # TODO - report error
#                            self.logger.debug("Error: unknown file descriptor")
#                oldvalue = knob_value
#                  
#            elif data == "stop":
#                self.keep_going = False  
#
#        os.close(self.knob_fd)
#        self.logger.debug('...stopping')

