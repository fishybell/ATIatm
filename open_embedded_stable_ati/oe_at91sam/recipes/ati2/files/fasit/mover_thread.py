MOVER_PATH          = "/sys/class/target/mover/"
MOVER_POSITION_PATH = "/sys/class/target/mover/"

import os
import select
import logging

from QThread import *
import fasit_packet_pd

#------------------------------------------------------------------------------
# 
#------------------------------------------------------------------------------
class mover_thread(QThread):    
    POLL_TIMEOUT_MS = 200
    
    def __init__(self, mover_type):
        QThread.__init__(self)
        self.logger = logging.getLogger('mover_thread')
        self.keep_going = True
        self.mover_type = mover_type
        self.mover_pos_mult = 1/50.0
        self.mover_speed_mult = 1/3750.0
        if (mover_type == "infantry"):
            self.mover_pos_mult = 1/25.0
            self.mover_speed_mult = 1/2000.0
        self.driver_path = MOVER_PATH
        self.driver_pos_path = MOVER_POSITION_PATH
        self.movement_path = self.driver_path + "movement"
        self.position_path = self.driver_pos_path + "position"
        self.speed_write_path = self.driver_path + "speed"
        self.speed_read_path = self.driver_pos_path + "velocity"
        self.movement_fd = None
        self.position_fd = None
        
    def check_driver(self):
        if (os.path.exists(self.driver_path) == False):
            self.logger.debug("sysfs path to driver does not exist (%s)", self.driver_path)
            raise ValueError('Path to mover driver not found.')
        
        # make sure this is the correct type of mover
        fd = os.open(self.driver_path + "type", os.O_RDONLY)
        type = os.read(fd, 32)
        os.close(fd)
        
        if (type != self.mover_type+"\n"):
            self.logger.debug("Wrong mover type: %s", type)
            raise ValueError('Wrong mover type.')
        
        self.movement_fd = os.open(self.movement_path,os.O_RDWR)
        self.position_fd = os.open(self.position_path,os.O_RDONLY)
        self.logger.debug('correct type...')
        
    # not to be called from within the thread context
    def get_setting_speed(self):
        fd = None 
        fd = os.open(self.speed_read_path, os.O_RDONLY)
        speed = int(abs(int(os.read(fd, 32)) * self.mover_speed_mult))
        os.close(fd)
        self.logger.debug("Current speed: %i", speed)
        return speed
    
    def set_setting_speed(self, speed):
        speed = int(speed)
        fd = None
        fd = os.open(self.speed_write_path, os.O_RDWR)
        os.write(fd, str(speed) + "\n")
        os.close(fd)

    # not to be called from within the thread context
    def get_current_movement(self):
        # get the current movement and report
        fd = None
        fd = os.open(self.movement_path, os.O_RDWR)
        movement = os.read(fd, 32)
        os.close(fd)
        
        self.logger.debug("Current movement status: %s", movement.rstrip())
        
        if (movement == "stopped\n"):
            return fasit_packet_pd.PD_MOVE_STOP
            
        elif (movement == "forward\n"):
           return fasit_packet_pd.PD_MOVE_FORWARD

        elif (movement == "reverse\n"):
            return fasit_packet_pd.PD_MOVE_REVERSE
        
        elif (movement == "fault\n"):
            return fasit_packet_pd.PD_MOVE_STOP
            # TODO - return actual fault code from driver
            #return -1*fasit_packet_pd.PD_FAULT_XXXXXXX
       
        else:
            self.logger.debug("unknown movement %s", movement.rstrip())
            return -1*fasit_packet_pd.PD_FAULT_INVALID_EXCEPTION_CODE_RETURNED

        
    def run(self):
        self.logger.debug('mover thread running...')
        
        # process incoming commands            
        while self.keep_going:
            data = self.read_in_blocking()
            if data == fasit_packet_pd.PD_MOVE_FORWARD:
                self.logger.debug('mover forward command')
                self.send_command_and_wait("forward")
            elif data == fasit_packet_pd.PD_MOVE_REVERSE:
                self.logger.debug('mover reverse command')
                self.send_command_and_wait("reverse")
#            elif data == fasit_packet_pd.PD_MOVE_STOP:
#                self.logger.debug('mover stop command')
#                self.send_command_and_wait("stop")
            elif data == "stop":
                self.keep_going = False

        os.close(self.movement_fd)
        os.close(self.position_fd)
        self.logger.debug('...stopping')
                
    def send_command_and_wait(self, command):
# no longer do we just move if we were already stopped
#        os.lseek(self.movement_fd,0,os.SEEK_SET)
#        movement = os.read(self.movement_fd, 32)
#        
#        movement = movement.rstrip()
#        self.logger.debug("Current movement: %s", movement)
#        if ((command != "stop") and (movement != "stopped")):
#            self.logger.debug("Cannot move target %s when target mover status is %s", command, movement)
#            # TODO - write an error code back to TRACR...
#            return

        # write out the command...
        os.lseek(self.movement_fd,0,os.SEEK_SET)
        os.write(self.movement_fd, command)

        # if there is an error, return (the error gets reported back by the function)
        # TODO - check if stopped?
#        movement = self.get_current_movement_thread()
#        if  ((movement == "stopped") or (movement == "fault")):
#            self.logger.debug("stopped, fault, or error (%s)", movement)
#            return
        
        keep_going = True
        wait_for_stop = False
        while keep_going:
            data = self.read_in()
            
            if(data is None):
                # wait for a change - the driver will notify if the mover stops or for each
                # change in position
                os.lseek(self.movement_fd,0,os.SEEK_SET)
                movement = os.read(self.movement_fd, 32)
                
                #self.logger.debug("RACE?: movement = %s", movement)
                if (wait_for_stop == True):
                    if((movement == "stopped\n") or (movement == "fault\n")):
                        keep_going = False
                
                p = select.poll()
                
                os.lseek(self.movement_fd,0,os.SEEK_SET)
                p.register(self.movement_fd, select.POLLERR|select.POLLPRI)
                os.lseek(self.position_fd,0,os.SEEK_SET)
                p.register(self.position_fd, select.POLLERR|select.POLLPRI)
                
                s = p.poll(self.POLL_TIMEOUT_MS)
                
                while (len(s) > 0):
                    fd, event = s.pop()
                    
                    if (fd == self.movement_fd):
                        self.logger.debug("Change in movement...")

                        movement = self.get_current_movement_thread()
                        if ((movement == "stopped") or (movement == "fault")):
                            keep_going = False
                                            
                    elif (fd == self.position_fd):
                        self.logger.debug("Change in position...")
                        self.get_current_position_thread() 
                        
                    else:
                        # TODO - report error
                        self.logger.debug("Error: unknown file descriptor")
                        
            elif data == fasit_packet_pd.PD_MOVE_STOP:
                self.logger.debug('mover stop command')
                
                os.lseek(self.movement_fd,0,os.SEEK_SET)
                os.write(self.movement_fd, "stop")
                wait_for_stop = True
                    
            elif data == "stop":
                self.logger.debug('mover thread stop command')
                keep_going = False
                self.keep_going = False
              
            
    # called from within the thread context, the status is written out through the queue
    def get_current_movement_thread(self):
        
        # get the current movement and report
        os.lseek(self.movement_fd,0,os.SEEK_SET)
        movement = os.read(self.movement_fd, 32)
        
        movement = movement.rstrip()
        
        self.logger.debug("Current movement status: %s", movement)
        
        if (movement == "stopped"):
            self.write_out(fasit_packet_pd.PD_MOVE_STOP)
                        
        elif (movement == "forward"):
           self.write_out(fasit_packet_pd.PD_MOVE_FORWARD)
                  
        elif (movement == "reverse"):
            self.write_out(fasit_packet_pd.PD_MOVE_REVERSE)
                    
        elif (movement == "fault"):
            self.logger.debug("Need to send back a fault code here!")
            #self.write_out(-1*fasit_packet_pd.PD_FAULT_ACTUATION_WAS_NOT_COMPLETED)
            # TODO - send out fault code
                  
        else:
            self.logger.debug("unknown movement %s", movement)
            self.write_out(-1*fasit_packet_pd.PD_FAULT_INVALID_EXCEPTION_CODE_RETURNED)
            return "error"
        
        return movement
    
        # called from within the thread context, the status is written out through the queue
    def get_current_position_thread(self):
        
        # get the current movement and report
        os.lseek(self.position_fd,0,os.SEEK_SET)
        position = int(os.read(self.position_fd, 32)) * self.mover_pos_mult
        
        self.logger.debug("Current position: %i", position)
        
        self.write_out("position")
        self.write_out(int(position))
        
