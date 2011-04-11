from FasitPd import *
from mover_thread import *
import logging
import uuid

#------------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------------          
class FasitPdMit(FasitPd):
                        
    def __init__(self):
        FasitPd.__init__(self)
        self.logger = logging.getLogger('FasitPdMit')
       
        self.__device_id__          = uuid.getnode()
        self.__device_type__        = fasit_packet_pd.PD_TYPE_MIT
        
        self.__mover_thread__= mover_thread("infantry")
        self.__mover_thread__.check_driver()
            
        # get current mover movement       
        current_movement = self.__mover_thread__.get_current_movement() 
        if (current_movement < 0):
            self.__fault_code__ = abs(current_movement)   
        else:
            self.__move__ = current_movement
            
        self.__speed__ = self.__mover_thread__.get_setting_speed() 
       
        self.__mover_thread__.start()
        
    def stop_threads(self):
        self.__mover_thread__.write("stop")
        
        if (self.__mover_thread__.isAlive()):
            self.__mover_thread__.join()

    def move(self, direction = 0, movement = fasit_packet_pd.PD_MOVE_STOP, speed = 0):
        FasitPd.move(self, direction, movement, speed)
        self.__mover_thread__.set_setting_speed(speed)  
        self.__mover_thread__.write(movement)
        
    def check_for_updates(self):
        check_for_updates_status = False

        # check the mover thread
        mover_status = 0
        mover_status = self.__mover_thread__.read()
        if (mover_status is not None):
            if (mover_status < 0):
                mover_status = abs(mover_status)
                self.logger.debug("move error %d", mover_status)
                self.__fault_code__ = mover_status
                check_for_updates_status = True
                self.__move_needs_update__ = True
                
            elif (mover_status == "position"):
                new_position = self.__mover_thread__.read()
                self.logger.debug("position update %d", new_position)
                if (new_position != self.__position__):
                    self.__position__ = new_position
                    check_for_updates_status = True
                    self.__move_needs_update__ = True
                 
            elif (mover_status != self.__move__):
                self.logger.debug("Change in move status %d", mover_status)
                self.__move__ = mover_status
                check_for_updates_status = True
                self.__move_needs_update__ = True
                
        return check_for_updates_status   
 
    
