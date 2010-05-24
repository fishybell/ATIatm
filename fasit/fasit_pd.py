from threading import Thread
import Queue
import time
import logging

import fasit_packet_pd

class FasitPd():
    """FASIT Presentation Device
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
        self.__exposure_needs_update__    = False

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
        current_onoff = self.__hit_onoff__
        self.__hit_count__               = count
        self.__hit_onoff__               = onoff
        self.__hit_reaction__            = reaction
        self.__hits_to_kill__            = hits_to_kill
        self.__hit_sensitivity__         = sensitivity
        self.__hit_mode__                = mode
        self.__hit_burst_separation__    = burst_separation
        # react to changes...
        if (current_onoff != onoff):
            self.hit_enable(onoff)

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
    
    def writable(self):
        return False  
    
    def hit_enable(self, enable = fasit_packet_pd.PD_HIT_ONOFF_OFF):
        pass

    def expose(self, expose = fasit_packet_pd.PD_EXPOSURE_CONCEALED):
        if (expose != self.__exposure__):
            self.__exposure__ = fasit_packet_pd.PD_EXPOSURE_TRANSITION
      
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
            writable_status = True
            self.__hit_needs_update__ = True
   
        
class QThread(Thread):

    def __init__(self):
        self.logger = logging.getLogger('QThread')
        Thread.__init__(self)
        self.daemon = True
        self.in_q = Queue.Queue() 
        self.out_q = Queue.Queue() 
        
    def run(self):
        pass

    def write(self, data):
        try:
            self.in_q.put(data, True, 2)
        except (Queue.Full):
            self.logger.debug('write to IN Q timed out, or Q was full')
    
    def read(self):
        try:
            data = self.out_q.get_nowait()
        except (Queue.Empty):
            data = None
        return data    
    
    def write_out(self, data):
        try:
            self.out_q.put(data, True, 2)
        except (Queue.Full):
            self.logger.debug('write to OUT Q timed out, or Q was full')
    
    def read_in(self):
        try:
            data = self.in_q.get_nowait()
        except (Queue.Empty):
            data = None
        return data


# TEST CODE -----------------------------------------------------------------------------------------------------
class FasitPdTestSit(FasitPd):
    
    class hit_thread(QThread):    
        def __init__(self, seconds = 1):
            QThread.__init__(self)
            self.logger = logging.getLogger('hit_thread')
            self.seconds = seconds
            self.keep_going = True
            self.enabled = False
            
        def run(self):
            self.logger.debug('hit thread running...')
            while self.keep_going:
                time.sleep(self.seconds)
                if self.enabled == True:
                    self.write_out(1)
                data = self.read_in()
                if data == "enable":
                    self.enabled = True
                elif data == "disable":
                    self.enabled = False
                elif data == "stop":
                    self.keep_going = False
                    self.logger.debug('...hit thread stopping')
                    
    class exposure_thread(QThread):    
        def __init__(self):
            QThread.__init__(self)
            self.logger = logging.getLogger('exposure_thread')
            self.keep_going = True
            
        def run(self):
            self.logger.debug('exposure thread running...')
            while self.keep_going:
                time.sleep(1)
                data = self.read_in()
                if data == fasit_packet_pd.PD_EXPOSURE_EXPOSED:
                    self.logger.debug('expose command')
                    time.sleep(2)
                    self.write_out(fasit_packet_pd.PD_EXPOSURE_EXPOSED)
                elif data == fasit_packet_pd.PD_EXPOSURE_CONCEALED:
                    self.logger.debug('conceal command')
                    time.sleep(2)
                    self.write_out(fasit_packet_pd.PD_EXPOSURE_CONCEALED)
                elif data == "stop":
                    self.keep_going = False
                    self.logger.debug('...exposure thread stopping')
                  
                        
    def __init__(self):
        FasitPd.__init__(self)
        self.logger = logging.getLogger('FasitPdTestSit')
        self.__capabilities__ = (fasit_packet_pd.PD_CAP_MILES_SHOOTBACK | fasit_packet_pd.PD_CAP_MUZZLE_FLASH)
        self.__device_type__ = fasit_packet_pd.PD_TYPE_SIT
        self.__hit_thread__ = FasitPdTestSit.hit_thread(3)
        self.__hit_thread__.start()
        self.__exposure_thread__ = FasitPdTestSit.exposure_thread()
        self.__exposure_thread__.start()
        
    def stop_threads(self):
        self.__hit_thread__.write("stop")
        self.__exposure_thread__.write("stop")
        time.sleep(1)
        
    def hit_enable(self, enable = False):
        self.logger.debug("hit enable() %s" % enable)
        if enable == True:
            self.__hit_thread__.write("enable")
        elif enable == False:
            self.__hit_thread__.write("disable")
    
    def expose(self, expose = fasit_packet_pd.PD_EXPOSURE_CONCEALED):
        FasitPd.expose(self, expose)
        self.__exposure_thread__.write(expose)
    
    def writable(self):
        writable_status = False
        
        # check the hit thread
        new_hits = self.__hit_thread__.read()
        if (new_hits > 0):
            FasitPd.new_hit_handler(self, new_hits)
            if (self.__hit_count__ >= self.__hits_to_kill__):
                self.hit_enable(False)
                self.expose(fasit_packet_pd.PD_EXPOSURE_CONCEALED) 
                    
        # check the exposure thread
        exposure_status = self.__exposure_thread__.read()
        if (exposure_status != None):
            if (exposure_status != self.__exposure__):
                print "change in exposure status %d" % exposure_status
                self.__exposure__ = exposure_status
                writable_status = True
                self.__exposure_needs_update__ = True
                
        return writable_status
            
    


    
    
    
    
    
    
    
    
    
        
    
    
    
    
    
    