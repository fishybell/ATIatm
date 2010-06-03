from threading import Thread
from threading import BoundedSemaphore
import Queue
import time
import logging
import os
import random
import uuid
import select

import fasit_packet_pd
import fasit_audio

AUDIO_DIRECTORY = "./sounds/"

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
        pass
#        if (expose != self.__exposure__):
#            self.__exposure__ = fasit_packet_pd.PD_EXPOSURE_TRANSITION
      
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

class play_sound_thread(QThread):  
      
    def __init__(self, number):
        QThread.__init__(self)
        logger_string = "play_sound_thread" + str(number)
        self.logger = logging.getLogger(logger_string)
        self.keep_going = True
        self.track_number = -1
        self.track_path = ""
        self.loop = 1
        self.random = False
        self.playing = False
        self.playing_semaphore = BoundedSemaphore(1)
        self.audio = fasit_audio.FasitAudio()
        
    def is_playing(self):
        self.playing_semaphore.acquire()
        playing = self.playing
        self.playing_semaphore.release() 
        return playing
        
    def set_playing(self, playing):
        self.playing_semaphore.acquire()
        self.playing = playing
        self.playing_semaphore.release() 
        
    def play_track(self, track_number, loop = 1, random = False):
        if (self.is_playing() == True):
            self.logger.debug("Can't play track, player is currently playing.")
            return False
        if ((track_number < 0) | (track_number > 62)):
            self.logger.debug('Track number out of range [0 - 62]: %d', track_path)
            return False
        if ((loop < 0) | (loop > 255)):
            self.logger.debug('loop out of range [0 - 255]: %d', loop)
            return False
        if ((random != True) & (random != False)):
            self.logger.debug('random out of range [True or False]: %d', random)
            return False
        
        self.track_path = AUDIO_DIRECTORY+str(track_number)+".mp3"
        if (os.path.isfile(self.track_path) == True):
            self.set_playing(True)
            self.track_number = track_number
            self.loop = loop
            self.random = random
            if (self.random == True):
                self.loop = 1
            self.read_in()    
            self.start()
            return True
        else:
            self.logger.debug('Could not find file %s', self.track_path)
            self.track_number = -1
            self.track_path = ""
            self.loop = 1
            self.random = False
            return False
        
    def run(self):
        self.logger.debug('play_sound_thread running...')
        if (self.track_path != ""):
            self.audio.play(self.track_path, self.loop)
            while (self.keep_going == True):
                if(self.audio.poll() == True):
                    self.logger.debug('Track has stopped playing: %s', self.track_path)
                    if (self.random == True):
                        wait = random.randint(1, 10)
                        time.sleep(wait)
                        self.audio.play(self.track_path, self.loop)
                    else:
                        self.keep_going = False
                        self.track_number = -1
                        self.track_path = ""
                        self.loop = 1
                        self.random = False
                data = self.read_in()
                self.logger.debug("data = %s", data)
                if data == "stop":
                    self.random = False
                    self.audio.stop()
                time.sleep(0.5)
            self.logger.debug('...play_sound_thread stopped.')
            self.set_playing(True)
        else:
            self.logger.debug('Track path not set before starting thread.')
            
#------------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------------          
class FasitPdSit(FasitPd):
    
    class exposure_thread(QThread):    
        def __init__(self):
            QThread.__init__(self)
            self.logger = logging.getLogger('exposure_thread')
            self.keep_going = True
            self.driver_path = "/sys/class/target/lifter/"
            self.position_path = self.driver_path + "position"
            self.fd = None
            
        def run(self):
            self.logger.debug('exposure thread running...')
            
            if (os.path.exists(self.driver_path) == False):
                self.logger.debug("sysfs path to driver does not exist (%s)", self.driver_path)
                self.write_out(-1)
                return
            
            # make sure this is the correct type of lifter
            self.fd = os.open(self.driver_path + "type", os.O_RDONLY)
            type = os.read(self.fd, 32)
            os.close(self.fd)
            if (type != "infantry\n"):
                self.logger.debug("Wrong lifter type: %s", type)
                self.write_out(-1)
                return
            
            self.logger.debug('correct type...')
            
            # get the current position and report
            self.fd = os.open(self.position_path, os.O_RDWR)
            position = os.read(self.fd, 32)
            os.close(self.fd)
            if (position != "error\n"):
                self.logger.debug("Current position status: %s", position)
                if (position == "up\n"):
                    self.write_out(fasit_packet_pd.PD_EXPOSURE_EXPOSED)
                elif (position == "down\n"):
                    self.write_out(fasit_packet_pd.PD_EXPOSURE_CONCEALED)
                elif (position == "moving\n"):
                    self.write_out(fasit_packet_pd.PD_EXPOSURE_TRANSITION)
                else:
                    self.write_out(-1)
                    return
            else:
                self.logger.debug("Position error.")
                self.write_out(-1)
                return
            
            self.logger.debug('processing incoming commands...')
            
            # process incoming commands            
            while self.keep_going:
                data = self.read_in()
                if data == fasit_packet_pd.PD_EXPOSURE_EXPOSED:
                    self.logger.debug('expose command')
                    self.send_command_and_wait("up")
                elif data == fasit_packet_pd.PD_EXPOSURE_CONCEALED:
                    self.logger.debug('conceal command')
                    self.send_command_and_wait("down")
                elif data == "stop":
                    self.keep_going = False
                    self.logger.debug('...exposure thread stopping')
                    
                time.sleep(1)
        
        def send_command_and_wait(self, command):
            self.fd = os.open(self.position_path,os.O_RDWR)
            position = os.read(self.fd, 32)
            os.close(self.fd)
            self.logger.debug("Current position: %s", position)
            if (((command == "up") & (position != "down\n")) | ((command == "down") & (position != "up\n"))):
                self.logger.debug("Cannot move target %s when target is status is %s", command, position)
                return

            self.fd = os.open(self.position_path,os.O_RDWR)
            os.write(self.fd, command)
            os.close(self.fd)
            self.fd = os.open(self.position_path,os.O_RDWR)
            position = os.read(self.fd, 32)
            
            if (position == "up\n"):
                self.write_out(fasit_packet_pd.PD_EXPOSURE_EXPOSED)
            elif (position == "down\n"):
                self.write_out(fasit_packet_pd.PD_EXPOSURE_CONCEALED)
            elif (position == "moving\n"):
                self.write_out(fasit_packet_pd.PD_EXPOSURE_TRANSITION)
            else:
                self.write_out(-1)
                return
            
            p = select.poll()
            p.register(self.fd, select.POLLERR|select.POLLPRI)
            s = p.poll()
            os.close(self.fd)
            d = os.open(self.position_path,os.O_RDWR )
            position = os.read(self.fd, 32) 
            
            self.logger.debug("Current position status: %s", position)
        
            if (position == "up\n"):
                self.write_out(fasit_packet_pd.PD_EXPOSURE_EXPOSED)
            elif (position == "down\n"):
                self.write_out(fasit_packet_pd.PD_EXPOSURE_CONCEALED)
            elif (position == "moving\n"):
                self.write_out(fasit_packet_pd.PD_EXPOSURE_TRANSITION)
            else:
                self.write_out(-1)
                
                  
                        
    def __init__(self):
        FasitPd.__init__(self)
        self.logger = logging.getLogger('FasitPdSit')
        self.__device_id__          = uuid.getnode()
        self.__device_type__        = fasit_packet_pd.PD_TYPE_SIT
        self.__exposure_thread__    = FasitPdSit.exposure_thread()
        self.__exposure_thread__.start()
        
    def stop_threads(self):
        self.__exposure_thread__.write("stop")
        
    def expose(self, expose = fasit_packet_pd.PD_EXPOSURE_CONCEALED):
        FasitPd.expose(self, expose)
        self.__exposure_thread__.write(expose)
    
    def writable(self):
        writable_status = False
                    
        # check the exposure thread
        exposure_status = self.__exposure_thread__.read()
        if (exposure_status != None):
            if (exposure_status == -1):
                self.logger.debug("Exposure error")
                self.__fault_code__ = fasit_packet_pd.PD_FAULT_ACTUATION_WAS_NOT_COMPLETED
                writable_status = True
                self.__exposure_needs_update__ = True
            elif (exposure_status != self.__exposure__):
                self.logger.debug("change in exposure status %d", exposure_status)
                self.__exposure__ = exposure_status
                writable_status = True
                self.__exposure_needs_update__ = True
                
        return writable_status
    
    

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
            if (sound_thread == None):
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
            if ((self.__sound_thread_1__.is_playing() == True) & (self.__sound_thread_1__.track_number == track_number)):  
                self.__sound_thread_1__.write("stop")
            if ((self.__sound_thread_2__.is_playing() == True) & (self.__sound_thread_2__.track_number == track_number)): 
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
