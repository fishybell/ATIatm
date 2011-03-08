import time
import logging

import fasit_packet_pd
from remote_target import *


#------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------
class FasitPd():
    """base class for FASIT Presentation Devices
    """
    def __init__(self):
        self.logger = logging.getLogger('FasitPd')
        
        self.__device_id__           = 0xDEADBEEF
        
        self.__capabilities__        = fasit_packet_pd.PD_CAP_NONE
        self.__power_status__        = fasit_packet_pd.PD_POWER_SHORE_POWER
        self.__fault_code__          = fasit_packet_pd.PD_FAULT_NORMAL
        self.__exposure__            = fasit_packet_pd.PD_EXPOSURE_CONCEALED
        self.__aspect__              = 0
        self.__direction__           = 0
        self.__move__                = fasit_packet_pd.PD_MOVE_STOP
        self.__move_speed__          = 0
        self.__position__            = 0
        self.__device_type__         = fasit_packet_pd.PD_TYPE_NONE
        
        self.__hit_count__               = 0
        self.__hit_onoff__               = fasit_packet_pd.PD_HIT_ONOFF_OFF
        self.__hit_reaction__            = fasit_packet_pd.PD_HIT_REACTION_FALL
        self.__hits_to_kill__            = 1
        self.__hit_sensitivity__         = 1
        self.__hit_mode__                = fasit_packet_pd.PD_HIT_MODE_SINGLE
        self.__hit_burst_separation__    = 250
        
        self.__miles_basic_code__        = 0
        self.__miles_ammo_type__         = 0
        self.__miles_player_id__         = 1
        self.__miles_fire_delay__        = 0
        
        self.__muzzle_flash_onoff__          = 0
        self.__muzzle_flash_mode__           = fasit_packet_pd.PD_MUZZLE_FLASH_MODE_SINGLE
        self.__muzzle_flash_init_delay__     = 0
        self.__muzzle_flash_repeat_delay__   = 0    
        
        self.__hit_needs_update__       = False
        self.__move_needs_update__      = False
        self.__exposure_needs_update__  = False
        
    def __del__(self):
        self.stop_threads()

    def get_device_id(self):
        return self.__device_id__
    
    def get_device_capabilities(self):
        return self.__capabilities__
    
    def get_power_status(self):
        return self.__power_status__
    
    def get_fault_code(self):
        temp_fault_code = self.__fault_code__
        self.__fault_code__ = fasit_packet_pd.PD_FAULT_NORMAL
        return temp_fault_code
    
    def get_exposure(self):
        return self.__exposure__
    
    def get_aspect(self):
        return self.__aspect__
    
    def get_direction_setting(self):
        return self.__direction__
    
    def get_move_setting(self):
        return self.__move__
        
    def get_move_speed(self):
        return self.__move_speed__
    
    def get_position(self):
        return self.__position__
    
    def get_device_type(self):
        return self.__device_type__
    
    def get_hit_count(self):
        return self.__hit_count__
  
    def get_hit_onoff(self):
        return self.__hit_onoff__     

    def get_hit_reaction(self):
        return self.__hit_reaction__       

    def get_hits_to_kill(self):
        return self.__hits_to_kill__

    def get_hit_sensitivity(self):
        return self.__hit_sensitivity__
    
    def get_hit_mode(self):
        return self.__hit_mode__ 

    def get_hit_burst_separation(self):
        return self.__hit_burst_separation__ 

    def get_miles_basic_code(self):
        return self.__miles_basic_code__     
    
    def get_miles_ammo_type(self):
        return self.__miles_ammo_type__      

    def get_miles_player_id(self):
        return self.__miles_player_id__ 

    def get_miles_fire_delay(self):
        return self.__miles_fire_delay__               

    def get_muzzle_flash_onoff(self):
        return self.__muzzle_flash_onoff__   

    def get_muzzle_flash_mode(self):
        return self.__muzzle_flash_mode__ 

    def get_muzzle_flash_init_delay(self):
        return self.__muzzle_flash_init_delay__ 
    
    def get_muzzle_flash_repeat_delay(self):
        return self.__muzzle_flash_repeat_delay__ 
    
    def configure_hit_sensor(self, count, onoff, reaction, hits_to_kill, sensitivity, mode, burst_separation):
        
        change_in_settings = False
        
        if  ((self.__hit_onoff__                != onoff)               or     
             (self.__hit_sensitivity__          != sensitivity)         or
             (self.__hit_mode__                 != mode)                or
             (self.__hit_burst_separation__     != burst_separation)):
            change_in_settings = True

        if (change_in_settings == True):
            while (self.get_hit_enable() == True):
                self.set_hit_enable(False)
                time.sleep(0.3)
                
            
            if  (self.__hit_sensitivity__ != sensitivity):
                self.set_hit_sensitivity(sensitivity)
            if(self.__hit_mode__ != mode):
                self.set_hit_mode(mode)
            if(self.__hit_burst_separation__ != burst_separation):
                self.set_hit_burst_separation(burst_separation)
       
        self.__hit_count__               = count
        self.__hit_onoff__               = onoff
        self.__hit_reaction__            = reaction
        self.__hits_to_kill__            = hits_to_kill
        self.__hit_sensitivity__         = sensitivity
        self.__hit_mode__                = mode
        self.__hit_burst_separation__    = burst_separation

        if (self.__hit_onoff__ == fasit_packet_pd.PD_HIT_ONOFF_OFF):
            self.set_hit_enable(False)
        elif (self.__hit_onoff__ == fasit_packet_pd.PD_HIT_ONOFF_ON):
            self.set_hit_enable(True)
        
    def get_hit_enable(self):
        return False    
    
    def set_hit_enable(self, enable = fasit_packet_pd.PD_HIT_ONOFF_OFF):
        pass
    
    def set_hit_sensitivity(self, sensitivity):
        pass
    
    def set_hit_mode(self, mode):
        pass
    
    def set_hit_burst_separation(self, burst_separation):
        pass
    
    def set_miles_transmitter(self, on_off):
        pass
    
    def set_muzzle_flash(self, on_off):
        pass
  
    def configure_miles(self, basic_code, ammo_type, player_id, fire_delay):
        self.__miles_basic_code__        = basic_code
        self.__miles_ammo_type__         = ammo_type
        self.__miles_player_id__         = player_id
        self.__miles_fire_delay__        = fire_delay
    
    def configure_muzzle_flash(self, onoff, mode, initial_delay, repeat_delay):
        self.__muzzle_flash_onoff__          = onoff
        self.__muzzle_flash_mode__           = mode
        self.__muzzle_flash_init_delay__     = initial_delay
        self.__muzzle_flash_repeat_delay__   = repeat_delay
        
    def audio_command(self, function_code, track_number, volume, play_mode):
        pass
 
    def stop_threads(self):
        pass
    
    def check_for_updates(self):
        return False  

    def expose(self, expose = fasit_packet_pd.PD_EXPOSURE_CONCEALED):
        pass
    
    def move(self, direction = 0, movement = fasit_packet_pd.PD_MOVE_STOP, speed = 0):
        self.__direction__           = direction
        self.__move__                = movement
        self.__move_speed__          = speed
      
    def hit_needs_update(self):
        update = self.__hit_needs_update__
        self.__hit_needs_update__ = False
        return update
    
    def move_needs_update(self):
        update = self.__move_needs_update__
        self.__move_needs_update__ = False
        return update
    
    def exposure_needs_update(self):
        update = self.__exposure_needs_update__
        self.__exposure_needs_update__ = False
        return update
    
    def new_hit_handler(self, new_hits):
        if (new_hits > 0):
            print "new hit(s) %d" % new_hits
            self.__hit_count__ = self.__hit_count__ + new_hits
            self.__hit_needs_update__ = True
   

