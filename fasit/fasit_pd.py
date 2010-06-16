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

LIFTER_PATH         = "/sys/class/target/lifter/"
HIT_SENSOR_PATH     = "/sys/class/target/hit_sensor/"

AUDIO_DIRECTORY = "./sounds/"


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
        
        change_in_settings = False
        
        if (self.__hit_onoff__ != onoff):
            if (onoff == fasit_packet_pd.PD_HIT_ONOFF_OFF):
                self.hit_enable(False)
            elif (onoff == fasit_packet_pd.PD_HIT_ONOFF_ON):
                self.hit_enable(True)
        
        if  ((self.__hit_sensitivity__         != sensitivity)       |
            (self.__hit_mode__                != mode)              |
            (self.__hit_burst_separation__    != burst_separation)):
            change_in_settings = True
        
        self.__hit_count__               = count
        self.__hit_onoff__               = onoff
        self.__hit_reaction__            = reaction
        self.__hits_to_kill__            = hits_to_kill
        self.__hit_sensitivity__         = sensitivity
        self.__hit_mode__                = mode
        self.__hit_burst_separation__    = burst_separation

        return change_in_settings
        

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
   

#------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------       
class QThread(Thread):
    """Thread class with bi-directional queues for async communications
    """
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
    
    def read_in_blocking(self, wait_seconds = 1):
        try:
            data = self.in_q.get(block = True, timeout = wait_seconds)
        except (Queue.Empty):
            data = None
        return data

#------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------
class play_sound_thread(QThread):  
    """Class for playing sounds
    """      
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


# TODO - this class can be re-used by other lifter types with some small changes
#------------------------------------------------------------------------------
#
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
        self.fd = os.open(self.driver_path + "type", os.O_RDONLY)
        type = os.read(self.fd, 32)
        os.close(self.fd)
        if (type != self.lifter_type+"\n"):
            self.logger.debug("Wrong lifter type: %s", type)
            raise ValueError('Wrong lifter type.')
        
        self.logger.debug('correct type...')

    def get_current_position(self):
        if (self.isAlive()):
            self.logger.debug("Cannot get settings while thread is running")
            raise ValueError('Cannot get settings while thread is running')
        
        # get the current position and report
        self.fd = os.open(self.position_path, os.O_RDWR)
        position = os.read(self.fd, 32)
        os.close(self.fd)
        if (position != "error\n"):
            self.logger.debug("Current position status: %s", position.rstrip())
            if (position == "up\n"):
               return fasit_packet_pd.PD_EXPOSURE_EXPOSED
            elif (position == "down\n"):
                return fasit_packet_pd.PD_EXPOSURE_CONCEALED
            elif (position == "moving\n"):
                return fasit_packet_pd.PD_EXPOSURE_TRANSITION
            else:
                self.logger.debug("unknown position %s", position.rstrip())
                return -1
        else:
            self.logger.debug("Position error.")
            return -1
        
    def run(self):
        self.logger.debug('lifter thread running...')
        
        # process incoming commands            
        while self.keep_going:
            data = self.read_in_blocking()
            if data == fasit_packet_pd.PD_EXPOSURE_EXPOSED:
                self.logger.debug('expose command')
                self.send_command_and_wait("up")
            elif data == fasit_packet_pd.PD_EXPOSURE_CONCEALED:
                self.logger.debug('conceal command')
                self.send_command_and_wait("down")
            elif data == "stop":
                self.keep_going = False
                self.logger.debug('...lifter thread stopping')
                
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
            
# TODO - Currently works with mechanical hit sensor only      
#------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------     
class hit_thread(QThread):    
    HIT_POLL_TIMEOUT_MS = 500
    def __init__(self, sensor_type):
        QThread.__init__(self)
        self.logger = logging.getLogger('hit_thread')
        self.keep_going = True
        self.sensor_type = sensor_type
        self.driver_path = HIT_SENSOR_PATH
        self.hit_path = self.driver_path + "hit"
        self.fd = None
        
    def check_driver(self):
        if (os.path.exists(self.driver_path) == False):
            self.logger.debug("sysfs path to driver does not exist (%s)", self.driver_path)
            raise ValueError('Path to hit sensor driver not found.')
                    
        # make sure this is the correct type of hit sensor
        self.fd = os.open(self.driver_path + "type", os.O_RDONLY)
        type = os.read(self.fd, 32)
        os.close(self.fd)
        if (type != self.sensor_type+"\n"):
            self.logger.debug("Wrong hit sensor type: %s", type)
            raise ValueError('Wrong hit sensor type.')
       
        self.logger.debug('correct type...')
        
    def get_setting(self, setting_name):
        if (self.isAlive()):
            self.logger.debug("Cannot get settings while thread is running")
            raise ValueError('Cannot get settings while thread is running')
        
        # get the setting 
        self.fd = os.open(self.driver_path + setting_name, os.O_RDONLY)
        setting = os.read(self.fd, 32)
        os.close(self.fd)
        return setting
    
    def get_setting_enabled(self):
        setting = self.get_setting("enable")
        if (setting == "enabled\n"):
            self.logger.debug("Hit sensor is enabled")
            return True
        elif (setting == "disabled\n"):
            self.logger.debug("Hit sensor is disabled")
            return False
        else:
            self.logger.debug("Hit sensor unknown state (%s)", setting.rstrip())
            return 
        
    def get_setting_sensitivity(self):
            setting = self.get_setting("sensitivity")
            self.logger.debug("Hit sensor sensitivity = %s", setting.rstrip())
            return int(setting)

    def get_setting_burst_separation(self):
        setting = self.get_setting("burst_separation")
        self.logger.debug("Hit sensor burst_separation = %s", setting.rstrip())
        return int(setting)     

    def get_setting_mode(self):
        setting = self.get_setting("mode")
        if (setting == "single\n"):
            self.logger.debug("Hit sensor is in single mode")
            return fasit_packet_pd.PD_HIT_MODE_SINGLE
        elif (setting == "burst\n"):
            self.logger.debug("Hit sensor is in burst mode")
            return fasit_packet_pd.PD_HIT_MODE_BURST
        else:
            self.logger.debug("Hit sensor unknown mode (%s)", setting.rstrip())
            return 
        
    def run(self):
        self.logger.debug('hit thread running...')
        
        # clear the hit record if any by reading
#        self.fd = os.open(self.hit_path, os.O_RDONLY)
#        hits = os.read(self.fd, 32)
#        os.close(self.fd)
        
        # process incoming commands            
        while self.keep_going:
            data = self.read_in_blocking()
            if data == "enable":
                self.logger.debug('enable command')
                self.enable_and_wait()
            elif data == "stop":
                self.logger.debug('stop command')
                self.keep_going = False
                
        self.logger.debug('...hit thread stopping')
    
    def enable_and_wait(self):
        keep_going = True
        self.fd = os.open(self.driver_path + "enable",os.O_RDWR)
        os.write(self.fd, "enable")
        os.close(self.fd)
        
        self.fd = os.open(self.driver_path + "enable",os.O_RDWR)
        enable_state = os.read(self.fd, 32)
        os.close(self.fd)
        if (enable_state != "enabled\n"):
            self.logger.debug('error: sensor did not enable (%s)', enable_state)
            self.write_out(-1)
            return
        
        self.logger.debug('sensor is enabled')
        
        while keep_going:
            data = self.read_in()
            
            if(data == None):
                self.fd = os.open(self.hit_path,os.O_RDONLY)
                hits = os.read(self.fd, 32)
               
                if (int(hits) > 0):
                    self.logger.debug('hits: %i', int(hits))
                    self.write_out(int(hits))
                
                p = select.poll()
                p.register(self.fd, select.POLLERR|select.POLLPRI)
                s = p.poll(self.HIT_POLL_TIMEOUT_MS)
                os.close(self.fd)
                d = os.open(self.hit_path,os.O_RDONLY)
                hits = os.read(self.fd, 32)
               
                if (int(hits) > 0):
                    self.logger.debug('hits: %i', int(hits))
                    self.write_out(int(hits))
                    
            elif data == "disable":
                self.logger.debug('disable command')
                self.fd = os.open(self.driver_path + "enable",os.O_RDWR)
                os.write(self.fd, "disable")
                os.close(self.fd)
                
                self.fd = os.open(self.driver_path + "enable",os.O_RDWR)
                enable_state = os.read(self.fd, 32)
                os.close(self.fd)
                if (enable_state != "disabled\n"):
                    self.logger.debug('error: sensor did not disable')
                    self.write_out(-1)
                    
                keep_going = False
                    
            elif data == "stop":
                self.logger.debug('stop command')
                keep_going = False
                self.keep_going = False
      
                
            
            
#------------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------------          
class FasitPdSit(FasitPd):
                        
    def __init__(self):
        FasitPd.__init__(self)
        self.logger = logging.getLogger('FasitPdSit')
       
        self.__device_id__          = uuid.getnode()
        self.__device_type__        = fasit_packet_pd.PD_TYPE_SIT
        
        self.__lifter_thread__= lifter_thread("infantry")
        self.__lifter_thread__.check_driver()
       
        self.__hit_thread__ = hit_thread("mechanical")
        self.__hit_thread__.check_driver()
            
        # get current lifter position        
        self.__exposure__ = self.__lifter_thread__.get_current_position()     
        
        # get current hit sensor settings
        self.__hit_onoff__               = self.__hit_thread__.get_setting_enabled()
        self.__hit_sensitivity__         = self.__hit_thread__.get_setting_sensitivity()
        self.__hit_mode__                = self.__hit_thread__.get_setting_mode()
        self.__hit_burst_separation__    = self.__hit_thread__.get_setting_burst_separation()  
        
        self.__lifter_thread__.start()
        self.__hit_thread__.start()
        
    def stop_threads(self):
        self.__lifter_thread__.write("stop")
        self.__hit_thread__.write("stop")
        
    def expose(self, expose = fasit_packet_pd.PD_EXPOSURE_CONCEALED):
        FasitPd.expose(self, expose)
        self.__lifter_thread__.write(expose)
        
    def hit_enable(self, enable = False):
        self.logger.debug("hit sensor enable = %s" % enable)
        if enable == True:
            self.__hit_thread__.write("enable")
        elif enable == False:
            self.__hit_thread__.write("disable")
    
    def writable(self):
        writable_status = False
        
        # check the hit thread
        new_hits = self.__hit_thread__.read()
        if (new_hits > 0):
            FasitPd.new_hit_handler(self, new_hits)
            if (self.__hit_count__ >= self.__hits_to_kill__):
                self.hit_enable(False)
                self.expose(fasit_packet_pd.PD_EXPOSURE_CONCEALED) 
                    
        # check the lifter thread
        lifter_status = self.__lifter_thread__.read()
        if (lifter_status != None):
            if (lifter_status == -1):
                self.logger.debug("Exposure error")
                self.__fault_code__ = fasit_packet_pd.PD_FAULT_ACTUATION_WAS_NOT_COMPLETED
                writable_status = True
                self.__exposure_needs_update__ = True
            elif (lifter_status != self.__exposure__):
                self.logger.debug("change in exposure status %d", lifter_status)
                
                if ((self.__hit_onoff__ == fasit_packet_pd.PD_HIT_ONOFF_ON_POS) & (lifter_status == fasit_packet_pd.PD_EXPOSURE_EXPOSED)):
                    self.hit_enable(True)
                
                if ((self.__hit_onoff__ == fasit_packet_pd.PD_HIT_ONOFF_OFF_POS) & (lifter_status == fasit_packet_pd.PD_EXPOSURE_CONCEALED)):
                    self.hit_enable(False)
                
                self.__exposure__ = lifter_status
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
