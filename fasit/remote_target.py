import asyncore
import asynchat
import logging
import socket

import fasit_packet_pd
from asynchat_fasit_handler import FasitHandler

REMOTE_TARGET_SERVER_IP_ADDRESS     = '192.168.4.98'
REMOTE_TARGET_SERVER_IP_PORT        = 4444

class RemoteTargetServerHandler(FasitHandler):
    """RemoteTargetServerHandler
    """
    __device__ = None
    
    def __init__(self, sock):
        self.logger = logging.getLogger('RemoteTargetServerHandler')
        self.command_ack_packet = None
        self.command_status_packet = None
        
        FasitHandler.__init__(self, sock, name='RemoteTargetServerHandler')
        
        device_status_request = fasit_packet_pd.FasitPacketPd()
        status_request = fasit_packet_pd.FasitPacketPd.EventCommand()
        status_request.command_id = fasit_packet_pd.EVENT_CMD_REQ_STATUS
        device_status_request.data = status_request
        self.push(device_status_request.pack())
   
    def handle_close(self):
        # Lost connection to remote target
        # TODO - error - send error back to TRACR
        FasitHandler.handle_close(self)
        
    def command_ack_handler(self):
        self.logger.info('command_ack_handler()')
        self.command_ack_packet = fasit_packet_pd.FasitPacketPd(self.received_data)
        
    def device_status_handler(self):
        self.logger.info('device_status_handler()')
        self.command_status_packet = fasit_packet_pd.FasitPacketPd(self.received_data)
 
    _msg_num_to_handler = { 
        fasit_packet_pd.PD_EVENT_CMD_ACK    :command_ack_handler,
        fasit_packet_pd.PD_DEV_STATUS       :device_status_handler
        }

     
#------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------        
class RemoteTargetServer(asyncore.dispatcher):
  
    def __init__(self, address):
        asyncore.dispatcher.__init__(self)
        if (address == None):
            address = (REMOTE_TARGET_SERVER_IP_ADDRESS,REMOTE_TARGET_SERVER_IP_PORT)
        self.handler = None
        self.create_socket(socket.AF_INET, socket.SOCK_STREAM)
        self.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.bind(address)
        self.address = self.socket.getsockname()
        self.listen(1)
        return

    def handle_accept(self):
        # Called when a client connects to our socket
        if (self.handler != None):
            return
        client_info = self.accept()
        sock=client_info[0]
        self.handler = RemoteTargetServerHandler(sock)
        
    def handle_close(self):
        asyncore.dispatcher.handle_close(self)
        self.handler = None


        
