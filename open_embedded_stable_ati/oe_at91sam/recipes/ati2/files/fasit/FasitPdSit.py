MILES_TX_PATH       = "/sys/class/target/miles_transmitter/"
MUZZLE_FLASH_PATH   = "/sys/class/target/muzzle_flash/"

from FasitPd import *
from hit_thread import *
from lifter_thread import *
from user_interface_thread import *
import logging
import os
import uuid


#------------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------------          
class FasitPdSit(FasitPd):
                        
    def __init__(self, dtype):
        FasitPd.__init__(self)
        self.logger = logging.getLogger('FasitPdSit')
       
        self.__device_id__          = uuid.getnode()
        self.__device_type__        = dtype
       
        self.__ui_thread__= user_interface_thread(False)
        self.__ui_thread__.check_driver()
#        self.__ui_thread__.set_setting("bit_status", "on")

        # Check if we have MILES TX
        if (os.path.exists(MILES_TX_PATH + "delay") == True):
            self.logger.debug("MILES Transmitter driver found.")
            self.__miles_fire_delay__ = self.get_setting_int(MILES_TX_PATH + "delay")
            self.__capabilities__ = (self.__capabilities__ | fasit_packet_pd.PD_CAP_MILES_SHOOTBACK)
        else:
            self.logger.debug("MILES Transmitter driver not found.")
            
        # Check if we have Muzzle Flash
        if (os.path.exists(MUZZLE_FLASH_PATH) == True):
            self.logger.debug("Muzzle Flash Simulator driver found.")
            if (self.get_setting_string(MUZZLE_FLASH_PATH + "state") == "on"):
                self.__muzzle_flash_onoff__ = 1
            if (self.get_setting_string(MUZZLE_FLASH_PATH + "mode") == "burst"):
                self.__muzzle_flash_mode__ = fasit_packet_pd.PD_MUZZLE_FLASH_MODE_BURST
            self.__muzzle_flash_init_delay__  = self.get_setting_int(MUZZLE_FLASH_PATH + "initial_delay")
            self.__muzzle_flash_repeat_delay__  = self.get_setting_int(MUZZLE_FLASH_PATH + "repeat_delay")    
            self.__capabilities__ = (self.__capabilities__ | fasit_packet_pd.PD_CAP_MUZZLE_FLASH)
        else:
            self.logger.debug("Muzzle Flash Simulator driver not found.")
       
        self.__lifter_thread__= lifter_thread("infantry")
        self.__lifter_thread__.check_driver()
        
        # Check which type of hit sensor is in use
        if (os.path.exists(HIT_SENSOR_PATH + "type") == True):
            self.logger.debug("Hit sensor driver found.")
            self.__hit_sensor_type__ = self.get_setting_string(HIT_SENSOR_PATH + "type")
            self.logger.debug("Hit sensor type is %s", self.__hit_sensor_type__)
       
        self.__hit_thread__ = hit_thread(self.__hit_sensor_type__)
        self.__hit_thread__.check_driver()
            
        # get current lifter position       
        current_position = self.__lifter_thread__.get_current_position() 
        if (current_position < 0):
            self.__fault_code__ = abs(current_position)   
        else:
            self.__exposure__ = current_position
        
        # get current hit sensor settings
        self.__hit_onoff__ = self.__hit_thread__.get_setting_enabled()
        if (self.__hit_sensor_type__ == "mechanical"):
            self.__hit_sensitivity__         = self.__hit_thread__.get_setting_sensitivity()
            self.__hit_mode__                = self.__hit_thread__.get_setting_mode()
            self.__hit_burst_separation__    = self.__hit_thread__.get_setting_burst_separation()  
        
        self.__lifter_thread__.start()
        self.__hit_thread__.start()
        self.__ui_thread__.start()
#        self.__ui_thread__.set_setting("bit_status", "on")
        
    def get_setting_string(self, path):
        fd = os.open(path, os.O_RDONLY)
        setting = os.read(fd, 32)
        os.close(fd)
        return setting.rstrip()
    
    def get_setting_int(self, path):
        return int(self.get_setting_string(path))
    
    def set_setting_string(self, path, setting):
        fd = os.open(path, os.O_RDWR)
        os.write(fd, setting + "\n")
        os.close(fd)
    
    def set_setting_int(self, path, setting):
        self.set_setting_string(path, str(setting))
        
    def stop_threads(self):
        self.logger.info('stop_threads()')
        self.__lifter_thread__.write("stop")
        self.__hit_thread__.write("stop")
        self.__ui_thread__.write("stop")
        
        if (self.__lifter_thread__.isAlive()):
            self.__lifter_thread__.join()
            
        if (self.__hit_thread__.isAlive()):
            self.__hit_thread__.join()
            
        if (self.__ui_thread__.isAlive()):
            self.__ui_thread__.join()
        
        
    def expose(self, expose = fasit_packet_pd.PD_EXPOSURE_CONCEALED):
        FasitPd.expose(self, expose)
        self.__lifter_thread__.write(expose)
        
    def configure_miles(self, basic_code, ammo_type, player_id, fire_delay):
        FasitPd.configure_miles(self, basic_code, ammo_type, player_id, fire_delay)
        self.set_setting_int(MILES_TX_PATH + "delay", fire_delay)
        
    def configure_muzzle_flash(self, onoff, mode, initial_delay, repeat_delay):
        self.logger.info('FasitPdSit.config_muzzle_flash()')
        FasitPd.configure_muzzle_flash(self, onoff, mode, initial_delay, repeat_delay)
        
        if (mode == fasit_packet_pd.PD_MUZZLE_FLASH_MODE_SINGLE):
            self.set_setting_string(MUZZLE_FLASH_PATH + "mode", "single")
        else:
            self.set_setting_string(MUZZLE_FLASH_PATH + "mode", "burst")
            
        self.set_setting_int(MUZZLE_FLASH_PATH + "initial_delay", initial_delay)
        self.set_setting_int(MUZZLE_FLASH_PATH + "repeat_delay", repeat_delay)
        
    def get_hit_enable(self):
        return self.__hit_thread__.get_setting_enabled()
                
    def set_hit_enable(self, enable = False):
        self.logger.debug("hit sensor enable = %s" % enable)
        if enable == True:
            self.__hit_thread__.write("enable")
        elif enable == False:
            self.__hit_thread__.write("disable")
            
    def set_hit_sensitivity(self, sensitivity):
        return self.__hit_thread__.set_setting_sensitivity(sensitivity)
    
    def set_hit_mode(self, mode):
        return self.__hit_thread__.set_setting_mode(mode)
    
    def set_hit_burst_separation(self, burst_separation):
        return self.__hit_thread__.set_setting_burst_separation(burst_separation) 
    
    def set_miles_transmitter(self, on_off):
        self.set_setting_string(MILES_TX_PATH + "state", on_off)
        
    def set_muzzle_flash(self, on_off):
        self.set_setting_string(MUZZLE_FLASH_PATH + "state", on_off)
    
    def check_for_updates(self):
        check_for_updates_status = False
        
        # check the ui thread
        bit_status = self.__ui_thread__.read()
        if (bit_status == "pressed"):
            current_position = self.__lifter_thread__.get_current_position() 
            if (current_position == fasit_packet_pd.PD_EXPOSURE_CONCEALED):
                self.expose(fasit_packet_pd.PD_EXPOSURE_EXPOSED) 
            elif (current_position == fasit_packet_pd.PD_EXPOSURE_EXPOSED):
                self.expose(fasit_packet_pd.PD_EXPOSURE_CONCEALED)
            else:
                self.expose(fasit_packet_pd.PD_EXPOSURE_EXPOSED) 
        
        # check the hit thread
        new_hits = self.__hit_thread__.read()
        if ((new_hits is not None) and (new_hits > 0)):
            FasitPd.new_hit_handler(self, new_hits)
            if (self.__hit_count__ >= self.__hits_to_kill__):
                #self.set_hit_enable(False)
                self.logger.debug("hits-to-kill exceeded %d", self.__hit_count__)
                if (self.__hit_reaction__ != fasit_packet_pd.PD_HIT_REACTION_STOP):
                    self.expose(fasit_packet_pd.PD_EXPOSURE_CONCEALED) 
                    
            elif (self.__hit_reaction__ == fasit_packet_pd.PD_HIT_REACTION_BOB):
                    self.expose(fasit_packet_pd.PD_EXPOSURE_CONCEALED)
                    
        # check the lifter thread
        lifter_status = 0
        lifter_status = self.__lifter_thread__.read()
        if (lifter_status is not None):
            if (lifter_status < 0):
                lifter_status = abs(lifter_status)
                self.logger.debug("Exposure error %d", lifter_status)
                self.__fault_code__ = lifter_status
                check_for_updates_status = True
                self.__exposure_needs_update__ = True
            elif (lifter_status != self.__exposure__):
                self.logger.debug("Change in exposure status %d", lifter_status)
                
                if (lifter_status == fasit_packet_pd.PD_EXPOSURE_EXPOSED):
                    #if (self.__hit_onoff__ == fasit_packet_pd.PD_HIT_ONOFF_ON_POS):
                    # TODO - right now we just enable the hit sensor when up, awaiting clarification 
                    self.set_hit_enable(True)
                        
                    if (self.__capabilities__ & fasit_packet_pd.PD_CAP_MILES_SHOOTBACK == fasit_packet_pd.PD_CAP_MILES_SHOOTBACK):
                        self.set_miles_transmitter("on")
                        
                    if ((self.__capabilities__ & fasit_packet_pd.PD_CAP_MUZZLE_FLASH == fasit_packet_pd.PD_CAP_MUZZLE_FLASH)
                        and (self.__muzzle_flash_onoff__ == 1)):
                        self.set_muzzle_flash("on")
                        
                if (lifter_status == fasit_packet_pd.PD_EXPOSURE_CONCEALED):
                    #if (self.__hit_onoff__ == fasit_packet_pd.PD_HIT_ONOFF_OFF_POS):
                    # TODO - right now we just disable the hit sensor when down, awaiting clarification 
                    self.set_hit_enable(False)
                        
                    if (self.__capabilities__ & fasit_packet_pd.PD_CAP_MILES_SHOOTBACK == fasit_packet_pd.PD_CAP_MILES_SHOOTBACK):
                        self.set_miles_transmitter("off")
                        
                    if (self.__capabilities__ & fasit_packet_pd.PD_CAP_MUZZLE_FLASH == fasit_packet_pd.PD_CAP_MUZZLE_FLASH):
                        self.set_muzzle_flash("off")
                        
                    #self.logger.debug("hit count = %d, htk = %d", self.__hit_count__, self.__hits_to_kill__)
                    #self.logger.debug("self.__hit_reaction__ = %d", self.__hit_reaction__)
                    if ((self.__hit_count__ < self.__hits_to_kill__) and
                        (self.__hit_reaction__ == fasit_packet_pd.PD_HIT_REACTION_BOB)):
                        self.expose(fasit_packet_pd.PD_EXPOSURE_EXPOSED)
                
                self.__exposure__ = lifter_status
                check_for_updates_status = True
                self.__exposure_needs_update__ = True
                
        return check_for_updates_status
  
    
