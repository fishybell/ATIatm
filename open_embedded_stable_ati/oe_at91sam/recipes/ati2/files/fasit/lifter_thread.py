LIFTER_PATH         = "/sys/class/target/lifter/"

import os
import select
import logging

from QThread import *
import fasit_packet_pd

#------------------------------------------------------------------------------
# TODO - this class can be re-used by other lifter types with some small changes
#------------------------------------------------------------------------------
class lifter_thread(QThread):    
    def __init__(self, lifter_type):
        QThread.__init__(self)
        self.logger = logging.getLogger('lifter_thread')
        self.keep_going = True
        self.lifter_type = lifter_type
        self.driver_path = LIFTER_PATH
        self.position_path = self.driver_path + "position"
        self.fd = None
        
    def check_driver(self):
        if (os.path.exists(self.driver_path) == False):
            self.logger.debug("sysfs path to driver does not exist (%s)", self.driver_path)
            raise ValueError('Path to lifter driver not found.')
        
        # make sure this is the correct type of lifter
        fd = os.open(self.driver_path + "type", os.O_RDONLY)
        type = os.read(fd, 32)
        os.close(fd)
        
        if (type != self.lifter_type+"\n"):
            self.logger.debug("Wrong lifter type: %s", type)
            raise ValueError('Wrong lifter type.')
        
        self.logger.debug('correct type...')
        self.fd = os.open(self.position_path, os.O_RDWR)

    # not to be called from within the thread context
    def get_current_position(self):
        # get the current position and report
        fd = None
        fd = os.open(self.position_path, os.O_RDONLY)
        position = os.read(fd, 32)
        os.close(fd)
        
        self.logger.debug("Current position status: %s", position.rstrip())
        
        if (position == "down\n"):
            return fasit_packet_pd.PD_EXPOSURE_CONCEALED
            
        elif (position == "up\n"):
           return fasit_packet_pd.PD_EXPOSURE_EXPOSED

        elif (position == "moving\n"):
            return fasit_packet_pd.PD_EXPOSURE_TRANSITION
        
        elif (position == "neither\n"):
            return -1*fasit_packet_pd.PD_FAULT_ACTUATION_WAS_NOT_COMPLETED
       
        else:
            self.logger.debug("unknown position %s", position.rstrip())
            return -1*fasit_packet_pd.PD_FAULT_INVALID_EXCEPTION_CODE_RETURNED

        
    def run(self):
        self.logger.debug('lifter thread running...')
        
        # process incoming commands            
        while self.keep_going:
            data = self.read_in_blocking()
            if data == fasit_packet_pd.PD_EXPOSURE_EXPOSED:
                self.logger.debug('expose command')
                self.send_command_and_wait("up") ;# start the move upwards, and wait 'til it arrives
            elif data == fasit_packet_pd.PD_EXPOSURE_CONCEALED:
                self.logger.debug('conceal command')
                self.send_command_and_wait("down") ;# start the move downwards, and wait 'til it arrives
            elif data == "stop":
                self.keep_going = False

        os.close(self.fd)
        self.logger.debug('...stopping')
                
    def send_command_and_wait(self, command):
        os.lseek(self.fd,0,os.SEEK_SET)
        position = os.read(self.fd, 32)
        
        self.logger.debug("Current position: %s", position)
        if (((command == "up") and (position == "up\n")) or ((command == "down") and (position == "down\n"))):
            self.logger.debug("Cannot move target %s when target status is %s", command, position)
            # we would write an error code back to TRACR, but no error code exists for this case for 
            # the infantry lifter either way, the target is already in the commanded position...
            return

        # write out the command...
        os.lseek(self.fd,0,os.SEEK_SET)
        os.write(self.fd, command)

        # if there is an error, return (the error gets reported back by the function)
        if (self.get_current_position_thread() == "error"):
            return
        
        # wait for a change - the driver will notify if there is a timeout
        # TODO - add out own timeout just in case?
        p = select.poll()
        os.lseek(self.fd,0,os.SEEK_SET)
        p.register(self.fd, select.POLLERR|select.POLLPRI)
        s = p.poll()
        
        # TODO - we need to notify TRACR that we're in a intermediate step
        position = self.get_current_position_thread()
        if (position == "neither\n"):
            if (command == "up"):
                self.write_out(-1*fasit_packet_pd.PD_FAULT_DID_NOT_REACH_EXPOSE_SWITCH)
                self.logger.debug("Error: did not reach expose switch")
            else:
                self.write_out(-1*fasit_packet_pd.PD_FAULT_DID_NOT_REACH_CONCEAL_SWITCH)
                self.logger.debug("Error: did not reach conceal switch")
                
        elif ((command == "up") and (position == "down\n")):
                self.write_out(-1*fasit_packet_pd.PD_FAULT_DID_NOT_LEAVE_CONCEAL_SWITCH)
                self.logger.debug("Error: did not leave conceal switch")
                
        elif ((command == "down") and (position == "up\n")):
                self.write_out(-1*fasit_packet_pd.PD_FAULT_DID_NOT_LEAVE_EXPOSE_SWITCH)
                self.logger.debug("Error: did not leave expose switch")
                
            
    # called from within the thread context, the status is written out through the queue
    def get_current_position_thread(self):
        
        # get the current position and report
        os.lseek(self.fd,0,os.SEEK_SET)
        position = os.read(self.fd, 32)
        
        self.logger.debug("Current position status: %s", position.rstrip())
        
        if (position == "down\n"):
            self.write_out(fasit_packet_pd.PD_EXPOSURE_CONCEALED)
                        
        elif (position == "up\n"):
           self.write_out(fasit_packet_pd.PD_EXPOSURE_EXPOSED)
                  
        elif (position == "moving\n"):
            self.write_out(fasit_packet_pd.PD_EXPOSURE_TRANSITION)
                    
        elif (position == "neither\n"):
            self.write_out(-1*fasit_packet_pd.PD_FAULT_ACTUATION_WAS_NOT_COMPLETED)
                   
        else:
            self.logger.debug("unknown position %s", position.rstrip())
            self.write_out(-1*fasit_packet_pd.PD_FAULT_INVALID_EXCEPTION_CODE_RETURNED)
            return "error"
        
        return position
            
     
