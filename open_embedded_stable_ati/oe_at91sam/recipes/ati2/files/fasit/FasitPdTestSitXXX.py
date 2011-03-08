from FasitPd import *
from QThread import *
from play_sound_thread import *
from user_interface_thread import *
import time
import logging

# TEST CODE -----------------------------------------------------------------------------------------------------
class FasitPdTestSitXXX(FasitPd):
    
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
        
        self.__sound_thread_1__ = play_sound_thread(1)
        self.__sound_thread_2__ = play_sound_thread(2)
      
    def get_sound_thread(self):  
        if (self.__sound_thread_1__.is_playing() == False):  
            return self.__sound_thread_1__
        elif (self.__sound_thread_2__.is_playing() == False):  
            return self.__sound_thread_2__
        else:
            return None
        
    def audio_command(self, function_code, track_number, volume, play_mode):
        
        if (function_code == fasit_packet_pd.PD_AUDIO_CMD_STOP_ALL):
            if (self.__sound_thread_1__.is_playing() == True):  
                self.__sound_thread_1__.write("stop")
            if (self.__sound_thread_2__.is_playing() == True):  
                self.__sound_thread_2__.write("stop")
            return True
       
        elif (function_code == fasit_packet_pd.PD_AUDIO_CMD_PLAY_TRACK):
            sound_thread = self.get_sound_thread()
            if (sound_thread is None):
                return False
            if (play_mode == fasit_packet_pd.PD_AUDIO_MODE_ONCE):
                return sound_thread.play_track(track_number, 1, False)
            elif (play_mode == fasit_packet_pd.PD_AUDIO_MODE_REPEAT):
                return sound_thread.play_track(track_number, 0, False)
            elif (play_mode == fasit_packet_pd.PD_AUDIO_MODE_RANDOM):
                return sound_thread.play_track(track_number, 1, True)
            else:
                return False
                
        elif (function_code == fasit_packet_pd.PD_AUDIO_CMD_STOP_TRACK):
            if ((self.__sound_thread_1__.is_playing() == True) and (self.__sound_thread_1__.track_number == track_number)):  
                self.__sound_thread_1__.write("stop")
            if ((self.__sound_thread_2__.is_playing() == True) and (self.__sound_thread_2__.track_number == track_number)): 
                self.__sound_thread_2__.write("stop")
            return True
        
        elif (function_code == fasit_packet_pd.PD_AUDIO_CMD_SET_VOLUME):
            return False
        
        else:
            return False
        
        return False
        
    def stop_threads(self):
        self.__hit_thread__.write("stop")
        self.__exposure_thread__.write("stop")
        self.__sound_thread_1__.write("stop")
        self.__sound_thread_2__.write("stop")
        time.sleep(1)
        
    def set_hit_enable(self, enable = False):
        self.logger.debug("hit enable() %s" % enable)
        if enable == True:
            self.__hit_thread__.write("enable")
        elif enable == False:
            self.__hit_thread__.write("disable")
    
    def expose(self, expose = fasit_packet_pd.PD_EXPOSURE_CONCEALED):
        FasitPd.expose(self, expose)
        self.__exposure_thread__.write(expose)
    
    def check_for_updates(self):
        check_for_updates_status = False
        
        # check the hit thread
        new_hits = self.__hit_thread__.read()
        if (new_hits > 0):
            FasitPd.new_hit_handler(self, new_hits)
            if (self.__hit_count__ >= self.__hits_to_kill__):
                self.set_hit_enable(False)
                self.expose(fasit_packet_pd.PD_EXPOSURE_CONCEALED) 
                    
        # check the exposure thread
        exposure_status = self.__exposure_thread__.read()
        if (exposure_status is not None):
            if (exposure_status != self.__exposure__):
                print "change in exposure status %d" % exposure_status
                self.__exposure__ = exposure_status
                check_for_updates_status = True
                self.__exposure_needs_update__ = True
                
        return check_for_updates_status
