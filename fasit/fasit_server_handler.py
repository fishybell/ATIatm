import asynchat
import logging
import socket

import dpkt
import fasit_packet
import fasit_packet_pd

from asynchat_fasit_handler import FasitHandler

class FasitServerHandler(FasitHandler):
    """FasitServerHandler
    """
    
    def __init__(self, sock):
        self.logger = logging.getLogger('FasitServerHandler')

        # responses should match these
#        self.message_number = fasit.FASIT_DEV_DEF_REQUEST
#        self.waiting_for_ack = False
        
        FasitHandler.__init__(self, sock, name='FasitServerHandler')
        
        # start off by sending a device definition request
        device_definition_request = fasit_packet.FasitPacket()
        device_definition_request.sequence_id = 0
        
        self.push(device_definition_request.pack())
        
        device_status_request = fasit_packet_pd.FasitPacketPd()
        status_request = fasit_packet_pd.FasitPacketPd.EventCommand()
        status_request.command_id = fasit_packet_pd.EVENT_CMD_REQ_STATUS
        
        
        audio_command_request = fasit_packet_pd.FasitPacketPd()
        audio_command = fasit_packet_pd.FasitPacketPd.AudioCommand()
        audio_command.function_code     = fasit_packet_pd.PD_AUDIO_CMD_SET_VOLUME
        audio_command.track_number      = 1
        audio_command.volume            = 50
        audio_command.play_mode         = fasit_packet_pd.PD_AUDIO_MODE_ONCE
        audio_command_request.data = audio_command
        self.push(audio_command_request.pack())
        
        audio_command.function_code     = fasit_packet_pd.PD_AUDIO_CMD_PLAY_TRACK
        audio_command_request.data = audio_command
        self.push(audio_command_request.pack())
        
        audio_command.function_code     = fasit_packet_pd.PD_AUDIO_CMD_SET_VOLUME
        audio_command.volume            = 100
        audio_command_request.data = audio_command
        self.push(audio_command_request.pack())
        
        audio_command.function_code     = fasit_packet_pd.PD_AUDIO_CMD_PLAY_TRACK
        audio_command.track_number      = 2
        audio_command_request.data = audio_command
        self.push(audio_command_request.pack())
        
#        audio_command.track_number      = 3
#        audio_command_request.data = audio_command
#        self.push(audio_command_request.pack())
#        

#        audio_command.function_code     = fasit_packet_pd.PD_AUDIO_CMD_STOP_TRACK
#        audio_command.track_number      = 0
#        audio_command_request.data = audio_command
#        self.push(audio_command_request.pack())
        
        return
