HIT_SENSOR_PATH     = "/sys/class/target/hit_sensor/"

import os
import select
import logging

from QThread import *
import fasit_packet_pd

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
        self.fd = None
        
    def check_driver(self):
        if (os.path.exists(self.driver_path) == False):
            self.logger.debug("sysfs path to driver does not exist (%s)", self.driver_path)
            raise ValueError('Path to hit sensor driver not found.')
                    
        # make sure this is the correct type of hit sensor
        type = self.get_setting("type")
        
        if (type != self.sensor_type+"\n"):
            self.logger.debug("Wrong hit sensor type: %s", type)
            raise ValueError('Wrong hit sensor type.')
       
        self.logger.debug('correct hit sensor type...')

        self.fd = os.open(self.driver_path + "hit",os.O_RDONLY)
        
    def get_setting(self, setting_name):
        # get the setting 
        settings_fd = os.open(self.driver_path + setting_name, os.O_RDONLY)
        setting = os.read(settings_fd, 32)
        os.close(settings_fd)
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
        settings_fd = os.open(self.driver_path + setting_name, os.O_RDWR)
        os.write(settings_fd, setting)
        os.close(settings_fd)
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
            # assume "disabled" at this point
            data = self.read_in_blocking()
            if data == "enable":
                self.logger.debug('enable command')
                self.enable_and_wait() ;# poll "hit" and continue checking message queue until "disable" or "stop" message
            elif data == "stop":
                self.logger.debug('stop command')
                self.keep_going = False
                
        os.close(self.fd)
        self.logger.debug('...stopping')
    
    def enable_and_wait(self):
        keep_going = True

        # change to "enabled" by writing "enable" to "enable" setting
        self.set_setting("enable","enable")
        
        # check that the driver properly enabled
        enable_state = self.get_setting("enable")
        if (enable_state != "enabled\n"):
            self.logger.debug('error: sensor did not enable (%s)', enable_state)
            self.write_out(-1)
            return
        
        self.logger.debug('sensor is enabled')
        
        # wait for "disable" or hit
        while keep_going:
            # read "disable," "stop" etc. from message queue
            data = self.read_in()
            
            if(data is None):
                # nothing in the queue, poll on the hit file descriptor for HIT_POLL_TIMEOUT_MS milliseconds
                p = select.poll()
                p.register(self.fd, select.POLLERR|select.POLLPRI)
                s = p.poll(self.HIT_POLL_TIMEOUT_MS)
                
                # check for hit vs. timeout
                if len(s) > 0 :
                    fd, event = s.pop()
                    if fd == self.fd :
                        # hit!
                        hits = os.read(self.fd, 32)
                        os.lseek(self.fd,0,os.SEEK_SET) ;# move back to beginning of file
                        if (int(hits) > 0):
                            self.logger.debug('hits: %i', int(hits))
                            self.write_out(int(hits))
                # timeout just continues...
     
            elif data == "disable":
                # disable command in message queue
                self.logger.debug('disable command')
                self.set_setting("enable","disable")
                
                # check that the driver properly disabled
                enable_state = self.get_setting("enable")
                if (enable_state != "disabled\n"):
                    self.logger.debug('error: sensor did not disable')
                    self.write_out(-1)
                    
                keep_going = False
                    
            elif data == "stop":
                # stop command in message queue
                self.logger.debug('stop command')
                keep_going = False ;# breaks out of this loop
                self.keep_going = False ;# breaks out of parent loop
  
  
