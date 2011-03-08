from FasitPd import *
from play_sound_thread import *
from user_interface_thread import *
from ses_interface_thread import *
import fasit_audio
import logging
import uuid


#------------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------------   
class FasitPdSes(FasitPd):
                        
    def __init__(self):
        FasitPd.__init__(self)
        self.logger = logging.getLogger('FasitPdSes')

        self.__device_id__          = uuid.getnode()
        self.__device_type__ = fasit_packet_pd.PD_TYPE_SES
       
        self.__sound_thread_1__ = play_sound_thread(1)
        self.__sound_thread_2__ = play_sound_thread(2)
      
        self.__ui_thread__= user_interface_thread(False)
        self.__ui_thread__.check_driver()
        self.__ui_thread__.start()

        self.__ses_thread__= ses_interface_thread()
        self.__ses_thread__.check_driver()
        self.__ses_thread__.start()

    def check_for_updates(self):
        check_for_updates_status = False
        
        # check the ui thread
        bit_status = self.__ui_thread__.read()
        if (bit_status == "pressed"):
            mode = self.__ses_thread__.get_setting("mode")
            self.logger.debug('bit_status seems to be -%s-, mode seems to be -%s-', bit_status, mode)
            self.audio_command(fasit_packet_pd.PD_AUDIO_CMD_STOP_ALL, 0, 0, 0)
            if (mode == "record\n"):
                self.logger.debug('recording audio mode')
                self.audio_command(fasit_packet_pd.PD_AUDIO_CMD_RECORD_TRACK, int(self.__ses_thread__.get_setting("knob")), 0, fasit_packet_pd.PD_AUDIO_MODE_ONCE)
            else:
                if (mode == "livefire\n"):
                    self.logger.debug('playing audio in livefire mode')
                    self.audio_command(fasit_packet_pd.PD_AUDIO_CMD_SET_VOLUME, 0, 100, 0)
                elif (mode == "testing\n"):
                    self.logger.debug('playing audio in testing mode')
                    self.audio_command(fasit_packet_pd.PD_AUDIO_CMD_SET_VOLUME, 0, 75, 0)
                elif (mode == "maintenance\n"):
                    self.logger.debug('playing audio in maintenance mode')
                    self.audio_command(fasit_packet_pd.PD_AUDIO_CMD_SET_VOLUME, 0, 50, 0)
                self.audio_command(fasit_packet_pd.PD_AUDIO_CMD_PLAY_TRACK, int(self.__ses_thread__.get_setting("knob")), 0, fasit_packet_pd.PD_AUDIO_MODE_ONCE)

        return check_for_updates_status

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
                self.__sound_thread_1__.write("stop_play")
            if (self.__sound_thread_2__.is_playing() == True):  
                self.__sound_thread_2__.write("stop_play")
            return True
       
        elif (function_code == fasit_packet_pd.PD_AUDIO_CMD_RECORD_TRACK):
            sound_thread = self.get_sound_thread()
            if (sound_thread is None):
                return False
            if (play_mode == fasit_packet_pd.PD_AUDIO_MODE_ONCE):
                return sound_thread.record_track(track_number)
            else:
                return False

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
                self.__sound_thread_1__.write("stop_play")
            if ((self.__sound_thread_2__.is_playing() == True) and (self.__sound_thread_2__.track_number == track_number)): 
                self.__sound_thread_2__.write("stop_play")
            return True
        
        elif (function_code == fasit_packet_pd.PD_AUDIO_CMD_SET_VOLUME):
            fasit_audio.set_volume(volume)
            return True
        
        else:
            return False
        
        return False
        
    def stop_threads(self):
        self.__sound_thread_1__.write("stop")
        self.__sound_thread_2__.write("stop")
        self.__ui_thread__.write("stop")
        
        if (self.__sound_thread_1__.isAlive()):
            self.__sound_thread_1__.join()
            
        if (self.__sound_thread_2__.isAlive()):
            self.__sound_thread_2__.join()

        if (self.__ui_thread__.isAlive()):
            self.__ui_thread__.join()
        
        

