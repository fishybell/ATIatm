import asynchat
import logging
import socket

import random

#import fasit_dpkt
import fasit_packet
import fasit_packet_pd_20

from asynchat_fasit_handler import FasitHandler

class FasitClient(FasitHandler):
    """FasitClient
    """
    
    def __init__(self, host, port):
        self.logger = logging.getLogger('FasitClient')

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.logger.debug('connecting to %s', (host, port))
        self.sock.connect((host, port))
        FasitHandler.__init__(self, sock=self.sock, name='FasitClient')
        return
    
    def __del__(self):
        self.sock.close()
        return
        
    def default_handler(self):
        self.logger.debug('default_handler()')
        event_cmd_ack = fasit_packet_pd_20.FasitPacketPd.EventCommandAck()
        event_cmd_ack.resp_message_number = self.temp_packet.message_number
        event_cmd_ack.resp_sequence_id = self.temp_packet.sequence_id
        self.temp_packet.sequence_id = self.get_new_sequence_id()
        self.temp_packet.data = event_cmd_ack
        self.push(self.temp_packet.pack())
    
    def dev_def_request_handler(self):
        self.logger.debug('dev_def_request_handler()')
        id_and_caps = fasit_packet_pd_20.FasitPacketPd.DeviceIdAndCapabilities()
        #id_and_caps.message_number = fasit_packet_pd_20.FASIT_PD_DEV_ID_AND_CAPS # should I have to do this?
        id_and_caps.resp_message_number = self.temp_packet.message_number
        id_and_caps.resp_sequence_id = self.temp_packet.sequence_id
        id_and_caps.device_id = random.randint(0, 0xFFFFFFFFFFFFFFFF)
        id_and_caps.device_type = fasit_packet_pd_20.FASIT_PD_TYPE_MAT
        id_and_caps.vendor_id = fasit_packet_pd_20.FASIT_PD_VENDOR_ID_ACTION_TARGET
        id_and_caps.fw_version_major = 0
        id_and_caps.fw_version_minor  = 1
        self.temp_packet.sequence_id = self.get_new_sequence_id()
        self.temp_packet.data = id_and_caps

#        print fasit_dpkt.hexdump(str(id_and_caps))
#        self.logger.debug(`self.temp_packet`)
        
        self.push(self.temp_packet.pack())
        
    def config_hit_sensor_handler(self):
        self.logger.debug('config_hit_sensor_handler()')
    
    def config_hit_sensor_status_handler(self):
        self.logger.debug('config_hit_sensor_status_handler()')
        
    def simple_handler(self):
        self.logger.debug('simple_handler()')
        # need to do a sanity check on the packet - build in to FASIT class?
        try:
            self._simple_cmd_to_handler[self.temp_packet.data.command_id](self, self.temp_packet.data.command_data)
        except (KeyError):
            self.simple_cmd_default_handler(self)
        
    def simple_cmd_default_handler(self):
        self.logger.debug('simple_cmd_default_handler()')
        
    def simple_cmd_none_handler(self, cmd_data):
        self.logger.debug('simple_cmd_none_handler()')
    
    def simple_cmd_expose_handler(self, cmd_data):
        self.logger.debug('simple_cmd_expose_handler()')
    
    def simple_cmd_conceal_handler(self, cmd_data):
        self.logger.debug('simple_cmd_conceal_handler()')
    
    def simple_cmd_req_full_status_handler(self, cmd_data):
        self.logger.debug('simple_cmd_req_full_status_handler()')
    
    def simple_cmd_req_general_status_handler(self, cmd_data):
        self.logger.debug('simple_cmd_req_general_status_handler()')
    
    def simple_cmd_req_hit_sensor_status_handler(self, cmd_data):
        self.logger.debug('simple_cmd_req_hit_sensor_status_handler()')
    
    def simple_cmd_req_hit_count_status_handler(self, cmd_data):
        self.logger.debug('simple_cmd_req_hit_count_status_handler()')
    
    def simple_cmd_req_movement_status_handler(self, cmd_data):
        self.logger.debug('simple_cmd_req_movement_status_handler()')
    
    def simple_cmd_req_miles_shootback_status_handler(self, cmd_data):
        self.logger.debug('simple_cmd_req_miles_shootback_status_handler()')
    
    def simple_cmd_req_mfs_status_handler(self, cmd_data):
        self.logger.debug('simple_cmd_req_mfs_status_handler()')
    
    def simple_cmd_aspect_set_handler(self, cmd_data):
        self.logger.debug('simple_cmd_aspect_set_handler()')
    
    def simple_cmd_mfs_on_handler(self, cmd_data):
        self.logger.debug('simple_cmd_mfs_on_handler()')
    
    def simple_cmd_mfs_off_handler(self, cmd_data):
        self.logger.debug('simple_cmd_mfs_off_handler()')
    
    def simple_cmd_thermal_on_handler(self, cmd_data):
        self.logger.debug('simple_cmd_thermal_on_handler()')
    
    def simple_cmd_thermal_off_handler(self, cmd_data):
        self.logger.debug('simple_cmd_thermal_off_handler()')
    
    def simple_cmd_move_forward_handler(self, cmd_data):
        self.logger.debug('simple_cmd_move_forward_handler()')
    
    def simple_cmd_move_reverse_handler(self, cmd_data):
        self.logger.debug('simple_cmd_move_reverse_handler()')
    
    def simple_cmd_move_stop_handler(self, cmd_data):
        self.logger.debug('simple_cmd_move_stop_handler()')
    
    def simple_cmd_hit_count_reset_handler(self, cmd_data):
        self.logger.debug('simple_cmd_hit_count_reset_handler()')
    
    def simple_cmd_hit_count_increment_handler(self, cmd_data):
        self.logger.debug('simple_cmd_hit_count_increment_handler()')
    
    def simple_cmd_hit_count_set_kill_handler(self, cmd_data):
        self.logger.debug('simple_cmd_hit_count_set_kill_handler()')    
    
    def simple_cmd_req_gps_location_handler(self, cmd_data):
        self.logger.debug('simple_cmd_req_gps_location_handler()')
    
    def simple_cmd_aux_dev_on_1_handler(self, cmd_data):
        self.logger.debug('simple_cmd_aux_dev_on_1_handler()')
    
    def simple_cmd_aux_dev_off_1_handler(self, cmd_data):
        self.logger.debug('simple_cmd_aux_dev_off_1_handler()')
    
    def simple_cmd_aux_dev_on_2_handler(self, cmd_data):
        self.logger.debug('simple_cmd_aux_dev_on_2_handler()')
    
    def simple_cmd_aux_dev_off_2_handler(self, cmd_data):
        self.logger.debug('simple_cmd_aux_dev_off_2_handler()')
    
    def simple_cmd_aux_dev_on_3_handler(self, cmd_data):
        self.logger.debug('simple_cmd_aux_dev_on_3_handler()')
    
    def simple_cmd_aux_dev_off_3_handler(self, cmd_data):
        self.logger.debug('simple_cmd_aux_dev_off_3_handler()')

        
    _msg_num_to_handler = { 
        fasit_packet.FASIT_DEV_DEF_REQUEST                      :dev_def_request_handler,
    fasit_packet_pd_20.FASIT_PD_SIMPLE_CMD                      :simple_handler,
        fasit_packet_pd_20.FASIT_PD_CONFIG_HIT_SENSOR           :config_hit_sensor_handler, 
        fasit_packet_pd_20.FASIT_PD_CONFIG_HIT_SENSOR_STATUS    :config_hit_sensor_status_handler,
        }
    
    _simple_cmd_to_handler = {
        fasit_packet_pd_20.SIMPLE_CMD_NONE                        : simple_cmd_none_handler,
        fasit_packet_pd_20.SIMPLE_CMD_EXPOSE                      : simple_cmd_expose_handler,
        fasit_packet_pd_20.SIMPLE_CMD_CONCEAL                     : simple_cmd_conceal_handler,
        fasit_packet_pd_20.SIMPLE_CMD_REQ_FULL_STATUS             : simple_cmd_req_full_status_handler,
        fasit_packet_pd_20.SIMPLE_CMD_REQ_GENERAL_STATUS          : simple_cmd_req_general_status_handler,
        fasit_packet_pd_20.SIMPLE_CMD_REQ_HIT_SENSOR_STATUS       : simple_cmd_req_hit_sensor_status_handler,
        fasit_packet_pd_20.SIMPLE_CMD_REQ_HIT_COUNT_STATUS        : simple_cmd_req_hit_count_status_handler,
        fasit_packet_pd_20.SIMPLE_CMD_REQ_MOVEMENT_STATUS         : simple_cmd_req_movement_status_handler,
        fasit_packet_pd_20.SIMPLE_CMD_REQ_MILES_SHOOTBACK_STATUS  : simple_cmd_req_miles_shootback_status_handler,
        fasit_packet_pd_20.SIMPLE_CMD_REQ_MFS_STATUS              : simple_cmd_req_mfs_status_handler,
        fasit_packet_pd_20.SIMPLE_CMD_ASPECT_SET                  : simple_cmd_aspect_set_handler,
        fasit_packet_pd_20.SIMPLE_CMD_MFS_ON                      : simple_cmd_mfs_on_handler,
        fasit_packet_pd_20.SIMPLE_CMD_MFS_OFF                     : simple_cmd_mfs_off_handler,
        fasit_packet_pd_20.SIMPLE_CMD_THERMAL_ON                  : simple_cmd_thermal_on_handler,
        fasit_packet_pd_20.SIMPLE_CMD_THERMAL_OFF                 : simple_cmd_thermal_off_handler,
        fasit_packet_pd_20.SIMPLE_CMD_MOVE_FORWARD                : simple_cmd_move_forward_handler,
        fasit_packet_pd_20.SIMPLE_CMD_MOVE_REVERSE                : simple_cmd_move_reverse_handler,
        fasit_packet_pd_20.SIMPLE_CMD_MOVE_STOP                   : simple_cmd_move_stop_handler,
        fasit_packet_pd_20.SIMPLE_CMD_HIT_COUNT_RESET             : simple_cmd_hit_count_reset_handler,
        fasit_packet_pd_20.SIMPLE_CMD_HIT_COUNT_INCREMENT         : simple_cmd_hit_count_increment_handler,
        fasit_packet_pd_20.SIMPLE_CMD_HIT_COUNT_SET_KILL          : simple_cmd_hit_count_set_kill_handler,
        fasit_packet_pd_20.SIMPLE_CMD_REQ_GPS_LOCATION            : simple_cmd_req_gps_location_handler,
        fasit_packet_pd_20.SIMPLE_CMD_AUX_DEV_ON_1                : simple_cmd_aux_dev_on_1_handler,
        fasit_packet_pd_20.SIMPLE_CMD_AUX_DEV_OFF_1               : simple_cmd_aux_dev_off_1_handler,
        fasit_packet_pd_20.SIMPLE_CMD_AUX_DEV_ON_2                : simple_cmd_aux_dev_on_2_handler,
        fasit_packet_pd_20.SIMPLE_CMD_AUX_DEV_OFF_2               : simple_cmd_aux_dev_off_2_handler,
        fasit_packet_pd_20.SIMPLE_CMD_AUX_DEV_ON_3                : simple_cmd_aux_dev_on_3_handler,
        fasit_packet_pd_20.SIMPLE_CMD_AUX_DEV_OFF_3               : simple_cmd_aux_dev_off_3_handler
        }
