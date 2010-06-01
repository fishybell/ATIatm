import asynchat
import logging
import socket

import random
import time

import fasit_packet
import fasit_packet_pd
import fasit_pd

from asynchat_fasit_handler import FasitHandler

class FasitClient(FasitHandler):
    """FasitClient
    """
    __device__ = None
    
    def __init__(self, host, port):
        self.logger = logging.getLogger('FasitClient')
        
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.logger.debug('connecting to %s', (host, port))
        
        self.sock.connect((host, port))
            
        #self.__device__ = fasit_pd.FasitPd()
        self.__device__ = fasit_pd.FasitPdTestSit()
        FasitHandler.__init__(self, sock=self.sock, name='FasitClient')
    
    def writable(self):
        if (self.__device__.writable() == True):
            if ((self.__device__.hit_needs_update() | 
                 self.__device__.move_needs_update() | 
                 self.__device__.exposure_needs_update())):
                self.send_device_status(False)
        return asynchat.async_chat.writable(self)
        
    def __del__(self):
        self.logger.debug('closing socket')
        self.sock.close()
        if (self.__device__ != None):
            self.__device__.stop_threads()
#        FasitHandler.__del__(self)
    
    def send_cmd_ack(self, ack = 'F'):
        self.logger.info('send_cmd_ack(%c)', ack)
        event_cmd_ack = fasit_packet_pd.FasitPacketPd.EventCommandAck()
        event_cmd_ack.ack_resp = ack
        event_cmd_ack.resp_message_number = self.in_packet.message_number
        event_cmd_ack.resp_sequence_id = self.in_packet.sequence_id
        self.in_packet.sequence_id = self.get_new_sequence_id()
        self.in_packet.data = event_cmd_ack
        self.push(self.in_packet.pack())
        
    def send_device_status(self, solicited = True):
        self.logger.info('send_device_status()')
        status = fasit_packet_pd.FasitPacketPd.DeviceStatus()
        self.out_packet = fasit_packet.FasitPacket()
        
        if (solicited == True):
            status.resp_message_number = self.in_packet.message_number
            status.resp_sequence_id = self.in_packet.sequence_id
            self.out_packet.sequence_id = self.get_new_sequence_id()
        else:
            self.out_packet.sequence_id = 0
        
        status.power_status         = self.__device__.get_power_status()
        status.oem_fault_code       = self.__device__.get_fault_code()
        status.exposure             = self.__device__.get_exposure()
        status.aspect               = self.__device__.get_aspect()
        status.direction            = self.__device__.get_direction_setting()
        status.move                 = self.__device__.get_move_setting()
        status.speed                = self.__device__.get_move_speed()
        status.track_position       = self.__device__.get_position()
        status.device_type          = self.__device__.get_device_type()
        status.hit_count            = self.__device__.get_hit_count()
        status.hit_onoff            = self.__device__.get_hit_onoff()
        status.hit_reaction         = self.__device__.get_hit_reaction()
        status.hits_to_kill         = self.__device__.get_hits_to_kill()
        status.hit_sensitivity      = self.__device__.get_hit_sensitivity()
        status.hit_mode             = self.__device__.get_hit_mode()
        status.hit_burst_separation = self.__device__.get_hit_burst_separation()        

        self.out_packet.data = status
        self.push(self.out_packet.pack())
        self.out_packet = None
        
    def send_miles_shootback_status(self):
        self.logger.info('send_miles_shootback_status()')
        miles_shootback_status = fasit_packet_pd.FasitPacketPd.MilesShootbackStatus()
        miles_shootback_status.resp_message_number = self.in_packet.message_number
        miles_shootback_status.resp_sequence_id = self.in_packet.sequence_id
        
        miles_shootback_status.basic_miles_code     = self.__device__.get_miles_basic_code()
        miles_shootback_status.ammo_type            = self.__device__.get_miles_ammo_type()
        miles_shootback_status.player_id            = self.__device__.get_miles_player_id()
        miles_shootback_status.fire_delay           = self.__device__.get_miles_fire_delay()
                
        self.in_packet.sequence_id = self.get_new_sequence_id()
        self.in_packet.data = miles_shootback_status
        self.push(self.in_packet.pack())

    def send_muzzle_flash_status(self):
        self.logger.info('send_muzzle_flash_status()')
        muzzle_flash_status = fasit_packet_pd.FasitPacketPd.MuzzleFlashStatus()

        muzzle_flash_status.resp_message_number = self.in_packet.message_number
        muzzle_flash_status.resp_sequence_id = self.in_packet.sequence_id
        
        muzzle_flash_status.on_off              = self.__device__.get_muzzle_flash_onoff()
        muzzle_flash_status.mode                = self.__device__.get_muzzle_flash_mode()
        muzzle_flash_status.initial_delay       = self.__device__.get_muzzle_flash_init_delay()
        muzzle_flash_status.repeat_delay        = self.__device__.get_muzzle_flash_repeat_delay()
        
        self.in_packet.sequence_id = self.get_new_sequence_id()
        self.in_packet.data = muzzle_flash_status
        self.push(self.in_packet.pack())
        
    def default_handler(self):
        self.logger.info('default_handler()')
        send_cmd_ack(ack = 'F')
    
    def dev_def_request_handler(self):
        self.logger.info('dev_def_request_handler()')
        id_and_caps = fasit_packet_pd.FasitPacketPd.DeviceIdAndCapabilities()
        id_and_caps.resp_message_number = self.in_packet.message_number
        id_and_caps.resp_sequence_id = self.in_packet.sequence_id
        
        id_and_caps.device_id = self.__device__.get_device_id()
        id_and_caps.device_capabilities = self.__device__.get_device_capabilities()

        self.in_packet.sequence_id = self.get_new_sequence_id()
        self.in_packet.data = id_and_caps
        
        self.push(self.in_packet.pack())
 
        
    def config_miles_shootback_handler(self):
        self.logger.info('config_miles_shootback_handler()')
        
        pd_packet = fasit_packet_pd.FasitPacketPd(self.received_data)
        
        #print `pd_packet`

        self.__device__.configure_miles(basic_code  = pd_packet.data.basic_miles_code, 
                                        ammo_type   = pd_packet.data.ammo_type,  
                                        player_id   = pd_packet.data.player_id,  
                                        fire_delay  = pd_packet.data.fire_delay)
        
        self.send_miles_shootback_status()

        
    def config_muzzle_flash_handler(self):
        self.logger.info('config_muzzle_flash_handler()')
        
        pd_packet = fasit_packet_pd.FasitPacketPd(self.received_data)
        
        #print `pd_packet`

        self.__device__.configure_muzzle_flash(onoff            = pd_packet.data.on_off,
                                               mode             = pd_packet.data.mode,
                                               initial_delay    = pd_packet.data.initial_delay,
                                               repeat_delay     = pd_packet.data.repeat_delay)
        
        self.send_muzzle_flash_status()
        
        
    def event_command_handler(self):
        self.logger.info('event_command_handler()')
       
        pd_packet = fasit_packet_pd.FasitPacketPd(self.received_data)
        
        #print `pd_packet`
        
        try:
            #self._event_cmd_to_handler[self.in_packet.data.command_id](self, self.in_packet.data.command_data)
            self._event_cmd_to_handler[pd_packet.data.command_id](self, pd_packet.data)
        except (KeyError):
            self.event_cmd_default_handler(self)
       
        #event_cmd = None   
          
        
    def event_cmd_default_handler(self):
        self.logger.info('event_cmd_default_handler()')
        send_cmd_ack(ack = 'F')
        
    def event_cmd_none_handler(self, cmd_data):
        self.logger.info('event_cmd_none_handler()')
        send_cmd_ack(ack = 'S')
        
    def event_cmd_reserved_handler(self, cmd_data):
        self.logger.info('event_cmd_reserved_handler()')
        send_cmd_ack(ack = 'F')
        
    def event_cmd_req_status_handler(self, cmd_data):
        self.logger.info('event_cmd_req_status_handler()')
        
        # always send device status
        self.send_device_status(True)
        
        # if the device supports MILES Shootback and/or Muzzle Flash,
        # we send status for them also
        capabilities = self.__device__.get_device_capabilities()
        if (capabilities & fasit_packet_pd.PD_CAP_MILES_SHOOTBACK):
            self.send_miles_shootback_status()
            
        if (capabilities & fasit_packet_pd.PD_CAP_MUZZLE_FLASH):
            self.send_muzzle_flash_status()    
        
    def event_cmd_req_expose_handler(self, cmd_data):
        self.logger.info('event_cmd_req_expose_handler()')
        self.__device__.expose(cmd_data.exposure)
        self.send_cmd_ack(ack = 'S')
        self.send_device_status(False)
    
    def event_cmd_req_dev_reset(self, cmd_data):
        self.logger.info('event_cmd_req_dev_reset()')
    
    def event_cmd_req_move(self, cmd_data):
        self.logger.info('event_cmd_req_move()')
    
    def event_cmd_config_hit_sensor_handler(self, cmd_data):
        self.logger.info('event_cmd_config_hit_sensor_handler()')
        self.__device__.configure_hit_sensor(count = cmd_data.hit_count,
                                             onoff = cmd_data.hit_onoff,
                                             reaction = cmd_data.hit_reaction,
                                             hits_to_kill = cmd_data.hits_to_kill,
                                             sensitivity = cmd_data.hit_sensitivity,
                                             mode = cmd_data.hit_mode,
                                             burst_separation = cmd_data.hit_burst_separation)
        
        self.send_cmd_ack(ack = 'S')
        self.send_device_status(False)
        
     
    def event_cmd_req_gps_location_handler(self, cmd_data):
        self.logger.info('event_cmd_req_gps_location_handler()')
        self.send_cmd_ack(ack = 'F')
        
    def audio_command_handler(self):
        self.logger.info('audio_command_handler()')
        
        pd_packet = fasit_packet_pd.FasitPacketPd(self.received_data)
        
        if (self.__device__.audio_command(  function_code   = pd_packet.data.function_code,
                                        track_number    = pd_packet.data.track_number,
                                        volume          = pd_packet.data.volume,
                                        play_mode       = pd_packet.data.play_mode) == True):
            self.send_cmd_ack(ack = 'S')
        else:
            self.send_cmd_ack(ack = 'F')

    _msg_num_to_handler = { 
        fasit_packet.FASIT_DEV_DEF_REQUEST              :dev_def_request_handler,
        fasit_packet_pd.PD_EVENT_CMD                    :event_command_handler,
        fasit_packet_pd.PD_CONFIG_MILES_SHOOTBACK       :config_miles_shootback_handler,
        fasit_packet_pd.PD_CONFIG_MUZZLE_FLASH          :config_muzzle_flash_handler,
        fasit_packet_pd.PD_AUDIO_CMD                    :audio_command_handler
        }
    
    _event_cmd_to_handler = {
        fasit_packet_pd.EVENT_CMD_NONE                  :event_cmd_none_handler,
        fasit_packet_pd.EVENT_CMD_RESERVED              :event_cmd_reserved_handler,
        fasit_packet_pd.EVENT_CMD_REQ_STATUS            :event_cmd_req_status_handler,
        fasit_packet_pd.EVENT_CMD_REQ_EXPOSE            :event_cmd_req_expose_handler,
        fasit_packet_pd.EVENT_CMD_REQ_DEV_RESET         :event_cmd_req_dev_reset,
        fasit_packet_pd.EVENT_CMD_REQ_MOVE              :event_cmd_req_move,
        fasit_packet_pd.EVENT_CMD_CONFIG_HIT_SENSOR     :event_cmd_config_hit_sensor_handler, 
        fasit_packet_pd.EVENT_CMD_REQ_GPS_LOCATION      :event_cmd_req_gps_location_handler,
        }

