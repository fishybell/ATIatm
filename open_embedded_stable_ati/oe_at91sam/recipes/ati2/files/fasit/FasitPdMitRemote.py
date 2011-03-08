from FasitPdSit import *
from remote_target import *
import logging
import uuid

#------------------------------------------------------------------------------------
#
#------------------------------------------------------------------------------------          
#class FasitPdMitRemote(FasitPd, RemoteTargetServer):
class FasitPdMitRemote(FasitPdSit, RemoteTargetServer):                        
    def __init__(self, local_ip, local_port):
        FasitPdSit.__init__(self, fasit_packet_pd.PD_TYPE_SIT)
        self.logger = logging.getLogger('FasitPdMitRemote')
       
        self.__device_id__          = uuid.getnode()
        self.__device_type__        = fasit_packet_pd.PD_TYPE_MIT
         
        RemoteTargetServer.__init__(self, (local_ip, local_port))
        
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
                   
        if (self.handler is not None):
            if (self.handler.command_status_packet is not None):
                self.logger.debug("Received status from remote target.")
                self.__fault_code__          = self.handler.command_status_packet.data.oem_fault_code
                self.__direction__           = self.handler.command_status_packet.data.direction
                self.__move__                = self.handler.command_status_packet.data.move
                self.__move_speed__          = self.handler.command_status_packet.data.speed
                self.__position__            = self.handler.command_status_packet.data.position
                self.handler.command_status_packet = None
                check_for_updates_status = True
                self.__move_needs_update__ = True
                
#            if (self.handler.command_ack_packet is not None)):
                      
        return check_for_updates_status
