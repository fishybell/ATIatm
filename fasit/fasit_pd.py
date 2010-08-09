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
from remote_target import *

LIFTER_PATH         = "/sys/class/target/lifter/"
MOVER_PATH          = "/sys/class/target/mover/"
HIT_SENSOR_PATH     = "/sys/class/target/hit_sensor/"
MILES_TX_PATH       = "/sys/class/target/miles_transmitter/"
MUZZLE_FLASH_PATH   = "/sys/class/target/muzzle_flash/"
USER_INTERFACE_PATH   = "/sys/class/target/user_interface/"

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
        if ((track_number < 0) or (track_number > 62)):
            self.logger.debug('Track number out of range [0 - 62]: %d', track_path)
            return False
        if ((loop < 0) or (loop > 255)):
            self.logger.debug('loop out of range [0 - 255]: %d', loop)
            return False
        if ((random != True) and (random != False)):
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
                #self.logger.debug("data = %s", data)
                if data == "stop":
                    self.random = False
                    self.audio.stop()
                time.sleep(0.5)
            self.logger.debug('...play_sound_thread stopped.')
            self.set_playing(True)
        else:
            self.logger.debug('Track path not set before starting thread.')


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
        self.fd = os.open(self.driver_path + "type", os.O_RDONLY)
        type = os.read(self.fd, 32)
        os.close(self.fd)
        
        if (type != self.lifter_type+"\n"):
            self.logger.debug("Wrong lifter type: %s", type)
            raise ValueError('Wrong lifter type.')
        
        self.logger.debug('correct type...')

    # not to be called from within the thread context
    def get_current_position(self):
        # get the current position and report
        fd = None
        fd = os.open(self.position_path, os.O_RDWR)
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
                self.send_command_and_wait("up")
            elif data == fasit_packet_pd.PD_EXPOSURE_CONCEALED:
                self.logger.debug('conceal command')
                self.send_command_and_wait("down")
            elif data == "stop":
                self.keep_going = False
                self.logger.debug('...stopping')
                
    def send_command_and_wait(self, command):
        self.fd = os.open(self.position_path,os.O_RDWR)
        position = os.read(self.fd, 32)
        os.close(self.fd)
        
        self.logger.debug("Current position: %s", position)
        if (((command == "up") and (position == "up\n")) or ((command == "down") and (position == "down\n"))):
            self.logger.debug("Cannot move target %s when target status is %s", command, position)
            # we would write an error code back to TRACR, but no error code exists for this case for 
            # the infantry lifter either way, the target is already in the commanded position...
            return

        # write out the command...
        self.fd = os.open(self.position_path,os.O_RDWR)
        os.write(self.fd, command)
        os.close(self.fd)

        # if there is an error, return (the error gets reported back by the function)
        if (self.get_current_position_thread(close_fd = False) == "error"):
            os.close(self.fd)
            return
        
        # wait for a change - the driver will notify if there is a timeout
        # TODO - add out own timeout just in case?
        p = select.poll()
        p.register(self.fd, select.POLLERR|select.POLLPRI)
        s = p.poll()
        os.close(self.fd)
        
        position = self.get_current_position_thread(close_fd = True)
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
    def get_current_position_thread(self, close_fd = True):
        
        # get the current position and report
        self.fd = os.open(self.position_path, os.O_RDWR)
        position = os.read(self.fd, 32)
        if (close_fd == True):                   
            os.close(self.fd)
        
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
            
     
#------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------     
class hit_thread(QThread):    
    HIT_POLL_TIMEOUT_MS = 200
    def __init__(self, sensor_type):
        QThread.__init__(self)
        self.logger = logging.getLogger('hit_thread')
        self.keep_going = True
        self.sensor_type = sensor_type
        self.driver_path = HIT_SENSOR_PATH
        self.hit_path = self.driver_path + "hit"
        self.fd = None
        self.settings_fd = None
        
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
       
        self.logger.debug('correct hit sensor type...')
        
    def get_setting(self, setting_name):
        # get the setting 
        self.settings_fd = os.open(self.driver_path + setting_name, os.O_RDONLY)
        setting = os.read(self.settings_fd, 32)
        os.close(self.settings_fd)
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
            return False
        
    def get_setting_sensitivity(self):
        if (self.sensor_type == "miles"):
            self.logger.debug("MILES hit does not support sensitivity setting")
            return 0
        else:
            setting = self.get_setting("sensitivity")
            self.logger.debug("Hit sensor sensitivity = %s", setting.rstrip())
            return int(setting)

    def get_setting_burst_separation(self):
        if (self.sensor_type == "miles"):
            self.logger.debug("MILES hit does not support burst separation setting")
            return 0
        else:
            setting = self.get_setting("burst_separation")
            self.logger.debug("Hit sensor burst_separation = %s", setting.rstrip())
            return int(setting)     

    def get_setting_mode(self):
        if (self.sensor_type == "miles"):
            self.logger.debug("MILES hit does not support mode setting")
            return fasit_packet_pd.PD_HIT_MODE_SINGLE
        else:
            setting = self.get_setting("mode")
            if (setting == "single\n"):
                self.logger.debug("Hit sensor is in single mode")
                return fasit_packet_pd.PD_HIT_MODE_SINGLE
            elif (setting == "burst\n"):
                self.logger.debug("Hit sensor is in burst mode")
                return fasit_packet_pd.PD_HIT_MODE_BURST
            else:
                self.logger.debug("Hit sensor unknown mode (%s)", setting.rstrip())
                return fasit_packet_pd.PD_HIT_MODE_SINGLE 
        
    def set_setting(self, setting_name, setting):
        if (self.get_setting_enabled() != False):
            self.logger.debug("Cannot set settings while sensor is enabled")
            return False
        
        # set the setting 
        self.settings_fd = os.open(self.driver_path + setting_name, os.O_RDWR)
        os.write(self.settings_fd, setting)
        os.close(self.settings_fd)
        return True
        
    def set_setting_sensitivity(self, sensitivity):
        if (self.sensor_type == "miles"):
            self.logger.debug("MILES hit does not support sensitivity setting")
            return False
        else:
            return self.set_setting("sensitivity", str(sensitivity) + "\n")

    def set_setting_burst_separation(self, burst_separation):
        if (self.sensor_type == "miles"):
            self.logger.debug("MILES hit does not support burst separation setting")
            return False
        else:
            return self.set_setting("burst_separation", str(burst_separation) + "\n")

    def set_setting_mode(self, mode):
        if (self.sensor_type == "miles"):
            self.logger.debug("MILES hit does not support mode setting")
            return False
        elif (mode == fasit_packet_pd.PD_HIT_MODE_SINGLE):
            setting = "single\n"
        elif (mode == fasit_packet_pd.PD_HIT_MODE_BURST):
            setting = "burst\n"
        else:
            self.logger.debug("Setting hit sensor unknown mode (%i)", mode)
            return False
        
        return self.set_setting("mode", setting)
        
    def run(self):
        self.logger.debug('hit thread running...')
        
        # process incoming commands            
        while self.keep_going:
            data = self.read_in_blocking()
            if data == "enable":
                self.logger.debug('enable command')
                self.enable_and_wait()
            elif data == "stop":
                self.logger.debug('stop command')
                self.keep_going = False
                
        self.logger.debug('...stopping')
    
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
                os.close(self.fd)
               
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
  
  
#------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------     
class user_interface_thread(QThread):    
    UI_POLL_TIMEOUT_MS = 500
    def __init__(self):
        QThread.__init__(self)
        self.logger = logging.getLogger('user_interface_thread')
        self.keep_going = True
        self.driver_path = USER_INTERFACE_PATH
        self.bit_button_path = self.driver_path + "bit_button"
        self.bit_button_fd = None
        
    def check_driver(self):
        if (os.path.exists(self.driver_path) == False):
            self.logger.debug("sysfs path to driver does not exist (%s)", self.driver_path)
            raise ValueError('Path to user interface driver not found.')

    def get_setting(self, setting_name):
        fd = os.open(self.driver_path + setting_name, os.O_RDONLY)
        setting = os.read(fd, 32)
        os.close(fd)
        return setting
        
    def set_setting(self, setting_name, setting):
        fd = os.open(self.driver_path + setting_name, os.O_RDWR)
        os.write(fd, setting)
        os.close(fd)
        return True
        
    def run(self):
        self.logger.debug('user interface thread running...')
        
        # clear any previous button presses on startup
        self.get_setting("bit_button")
        
        # process incoming commands            
        while self.keep_going:
            data = self.read_in()
            
            if(data == None):
                self.bit_button_fd = os.open(self.bit_button_path,os.O_RDONLY)
                bit_button_status = os.read(self.bit_button_fd, 32)
                
                if (bit_button_status == "pressed\n"):
                    self.logger.debug('bit button pressed')
                    self.write_out("pressed")
                    os.close(self.bit_button_fd)
                else:
                    p = select.poll()
                    p.register(self.bit_button_fd, select.POLLERR|select.POLLPRI)
                    s = p.poll(self.UI_POLL_TIMEOUT_MS)
                    os.close(self.bit_button_fd)
                    
                    while (len(s) > 0):
                        fd, event = s.pop()
                        
                        if (fd == self.bit_button_fd):
                            self.logger.debug("Change in bit button status...")
                            bit_button_status = self.get_setting("bit_button")
                            if (bit_button_status == "pressed\n"):
                                self.logger.debug('bit button pressed')
                                self.write_out("pressed")
                                                     
                        else:
                            # TODO - report error
                            self.logger.debug("Error: unknown file descriptor")
                  
            elif data == "stop":
                self.logger.debug('...stopping')
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
        
#        self.__ui_thread__= user_interface_thread()
#        self.__ui_thread__.check_driver()
#        self.__ui_thread__.set_setting("bit_status", "blink")

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
#        self.__ui_thread__.start()
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
#        self.__ui_thread__.write("stop")
        
        if (self.__lifter_thread__.isAlive()):
            self.__lifter_thread__.join()
            
        if (self.__hit_thread__.isAlive()):
            self.__hit_thread__.join()
            
#        if (self.__ui_thread__.isAlive()):
#            self.__ui_thread__.join()
        
        
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
#        bit_status = self.__ui_thread__.read()
#        if (bit_status == "pressed"):
#            current_position = self.__lifter_thread__.get_current_position() 
#            if (current_position == fasit_packet_pd.PD_EXPOSURE_CONCEALED):
#                self.expose(fasit_packet_pd.PD_EXPOSURE_EXPOSED) 
#            elif (current_position == fasit_packet_pd.PD_EXPOSURE_EXPOSED):
#                    self.expose(fasit_packet_pd.PD_EXPOSURE_CONCEALED)
        
        # check the hit thread
        new_hits = self.__hit_thread__.read()
        if ((new_hits != None) and (new_hits > 0)):
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
        if (lifter_status != None):
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
        self.driver_path = MOVER_PATH
        self.movement_path = self.driver_path + "movement"
        self.position_path = self.driver_path + "position"
        self.speed_path = self.driver_path + "speed"
        self.fd = None
        
    def check_driver(self):
        if (os.path.exists(self.driver_path) == False):
            self.logger.debug("sysfs path to driver does not exist (%s)", self.driver_path)
            raise ValueError('Path to mover driver not found.')
        
        # make sure this is the correct type of mover
        self.fd = os.open(self.driver_path + "type", os.O_RDONLY)
        type = os.read(self.fd, 32)
        os.close(self.fd)
        
        if (type != self.mover_type+"\n"):
            self.logger.debug("Wrong mover type: %s", type)
            raise ValueError('Wrong mover type.')
        
        self.logger.debug('correct type...')
        
    # not to be called from within the thread context
    def get_setting_speed(self):
        fd = None 
        fd = os.open(self.speed_path, os.O_RDONLY)
        speed = os.read(fd, 32)
        os.close(fd)
        speed = speed.rstrip()
        self.logger.debug("Current speed: %s", speed)
        return int(speed)
    
    def set_setting_speed(self, speed):
        speed = int(speed)
        fd = None
        fd = os.open(self.speed_path, os.O_RDWR)
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
            elif data == fasit_packet_pd.PD_MOVE_STOP:
                self.logger.debug('mover stop command')
                self.send_command_and_wait("stop")
            elif data == "stop":
                self.keep_going = False
                self.logger.debug('...stopping')
                
    def send_command_and_wait(self, command):
        self.fd = os.open(self.movement_path,os.O_RDWR)
        movement = os.read(self.fd, 32)
        os.close(self.fd)
        
        movement = movement.rstrip()
        self.logger.debug("Current movement: %s", movement)
        if ((command != "stop") and (movement != "stopped")):
            self.logger.debug("Cannot move target %s when target mover status is %s", command, movement)
            # TODO - write an error code back to TRACR...
            return

        # write out the command...
        self.fd = os.open(self.movement_path,os.O_RDWR)
        os.write(self.fd, command)
        os.close(self.fd)

        # if there is an error, return (the error gets reported back by the function)
        # TODO - check if stopped?
        movement = self.get_current_movement_thread(close_fd = True)
        if  ((movement == "stopped") or (movement == "fault") or (movement == "error")):
            #os.close(self.fd)
            return
        
        keep_going = True
        while keep_going:
            data = self.read_in()
            
            if(data == None):
                # wait for a change - the driver will notify if the mover stops or for each
                # change in position
                movement_fd = os.open(self.movement_path,os.O_RDONLY)
                position_fd = os.open(self.position_path,os.O_RDONLY)
                
                # TODO - catch any changes here?
                os.read(movement_fd, 32)
                os.read(position_fd, 32)
                
                p = select.poll()
                
                p.register(movement_fd, select.POLLERR|select.POLLPRI)
                p.register(position_fd, select.POLLERR|select.POLLPRI)
                
                s = p.poll(self.POLL_TIMEOUT_MS)
                
                os.close(movement_fd)
                os.close(position_fd)
                
                while (len(s) > 0):
                    fd, event = s.pop()
                    
                    if (fd == movement_fd):
                        self.logger.debug("Change in movement...")

                        movement = self.get_current_movement_thread(close_fd = True)
                        if ((movement == "stopped") or (movement == "fault") or (movement == "error")):
                            keep_going = False
                                            
                    elif (fd == position_fd):
                        self.logger.debug("Change in position...")
                        self.get_current_position_thread() 
                        
                    else:
                        # TODO - report error
                        self.logger.debug("Error: unknown file descriptor")
                        
            elif data == fasit_packet_pd.PD_MOVE_STOP:
                self.logger.debug('mover stop command')
                
                self.fd = os.open( self.movement_path ,os.O_RDWR)
                os.write(self.fd, "stop")
                os.close(self.fd)
                    
            elif data == "stop":
                self.logger.debug('mover thread stop command')
                keep_going = False
                self.keep_going = False
              
            
    # called from within the thread context, the status is written out through the queue
    def get_current_movement_thread(self, close_fd = True):
        
        # get the current movement and report
        self.fd = os.open(self.movement_path, os.O_RDWR)
        movement = os.read(self.fd, 32)
        if (close_fd == True):                   
            os.close(self.fd)
        
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
        pos_fd = os.open(self.position_path, os.O_RDONLY)
        position = os.read(pos_fd, 32)
        os.close(pos_fd)
        
        self.logger.debug("Current position: %s", position.rstrip())
        
        self.write_out("position")
        self.write_out(int(position))
        
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
        if (mover_status != None):
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
    
#------------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------------          
#class FasitPdMitRemote(FasitPd, RemoteTargetServer):
class FasitPdMitRemote(FasitPdSit, RemoteTargetServer):                        
    def __init__(self):
        FasitPdSit.__init__(self)
        self.logger = logging.getLogger('FasitPdMitRemote')
       
        self.__device_id__          = uuid.getnode()
        self.__device_type__        = fasit_packet_pd.PD_TYPE_MIT
         
        RemoteTargetServer.__init__(self, None)
        
#    def stop_threads(self):
#        # TODO - close connection?
#        pass
        
    def move(self, direction = 0, movement = fasit_packet_pd.PD_MOVE_STOP, speed = 0):
        FasitPd.move(self, direction, movement, speed)
        move_packet = fasit_packet_pd.FasitPacketPd()
        move_request = fasit_packet_pd.FasitPacketPd.EventCommand()
        move_request.command_id = fasit_packet_pd.EVENT_CMD_REQ_MOVE
        move_request.direction = direction
        move_request.move = movement
        move_request.speed = speed        
        move_packet.data = move_request
        self.handler.push(move_packet.pack())
        
    def check_for_updates(self):
        check_for_updates_status = FasitPdSit.check_for_updates(self)
                   
        if (self.handler != None):
            if (self.handler.command_status_packet != None):
                self.logger.debug("Received status from remote target.")
                self.__fault_code__          = self.handler.command_status_packet.data.oem_fault_code
                self.__direction__           = self.handler.command_status_packet.data.direction
                self.__move__                = self.handler.command_status_packet.data.move
                self.__move_speed__          = self.handler.command_status_packet.data.speed
                self.__position__            = self.handler.command_status_packet.data.position
                self.handler.command_status_packet = None
                check_for_updates_status = True
                self.__move_needs_update__ = True
                
#            if (self.handler.command_ack_packet != None)):
                      
        return check_for_updates_status
   
#------------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------------   
class FasitPdSes(FasitPd):
                        
    def __init__(self):
        FasitPd.__init__(self)
        self.logger = logging.getLogger('FasitPdSes')
        self.__device_type__ = fasit_packet_pd.PD_TYPE_SIT
       
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
            if ((self.__sound_thread_1__.is_playing() == True) and (self.__sound_thread_1__.track_number == track_number)):  
                self.__sound_thread_1__.write("stop")
            if ((self.__sound_thread_2__.is_playing() == True) and (self.__sound_thread_2__.track_number == track_number)): 
                self.__sound_thread_2__.write("stop")
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
        
        if (self.__sound_thread_1__.isAlive()):
            self.__sound_thread_1__.join()
            
        if (self.__sound_thread_2__.isAlive()):
            self.__sound_thread_2__.join()
        
        


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
        if (exposure_status != None):
            if (exposure_status != self.__exposure__):
                print "change in exposure status %d" % exposure_status
                self.__exposure__ = exposure_status
                check_for_updates_status = True
                self.__exposure_needs_update__ = True
                
        return check_for_updates_status
