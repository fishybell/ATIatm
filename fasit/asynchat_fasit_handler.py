import asynchat
import logging

#import dpkt
import fasit_packet
import fasit_packet_pd

class FasitHandler(asynchat.async_chat):
    """Handles fasit packets.
    """
    _msg_num_to_handler = {}
    
    def __init__(self, sock, name='FasitHandler'):
        self.sequence_id = 1
        self.in_packet = fasit_packet.FasitPacket()
        self.received_data = ''
        self.logger = logging.getLogger('%s %s' %(name, str(sock.getsockname())))
        self.logger.setLevel(logging.DEBUG)
        asynchat.async_chat.__init__(self, sock)
        # Start looking for the FASIT header 
        self.process_data = self._process_header
        self.set_terminator(self.in_packet.header_length())
        self.logger.debug("connected to %s" % str(sock.getpeername()))
        return
    
    def get_new_sequence_id(self):
        self.sequence_id = self.sequence_id + 1
        return self.sequence_id
    
    def handle_close(self):
        self.logger.debug("disconnected from server")
        asynchat.async_chat.handle_close(self)

    def collect_incoming_data(self, data):
        """Read an incoming message and put it into our outgoing queue."""
        self.logger.debug('collect_incoming_data() -> (%d)', len(data))
        self.received_data = self.received_data + data

    def found_terminator(self):
        """The end of a FASIT header has been seen."""
        self.logger.debug('found_terminator() -> (%d)', len(self.received_data))
        self.process_data()
    
    def _process_header(self):        
        """We _SHOULD_ have the full FASIT header"""
        self.logger.debug('_process_header() -> (%d)', len(self.received_data))
        
        self.in_packet = fasit_packet.FasitPacket(self.received_data)
        self.in_packet.data = ''
        self.logger.debug(`self.in_packet`)
        
        if len(self.received_data) == self.in_packet.length:
            # we have received all we need
            self._process_packet()
        else:
            # we need to wait for the rest of the packet
            self.set_terminator(self.in_packet.length-self.in_packet.header_length())
            self.process_data = self._process_packet
            
    def default_handler(self):
        self.logger.debug('default_handler()')
    
    def _process_packet(self):
        """We have read the entire packet"""
        self.logger.debug('_process_packet()')
        self.in_packet = fasit_packet.FasitPacket(self.received_data)
        
#        if self.in_packet.data:
#            print dpkt.hexdump(str(self.in_packet.data))
        
        pd_packet = fasit_packet_pd.FasitPacketPd(self.received_data)
        self.logger.debug(`pd_packet`)
        
        try:
            #self._msg_num_to_handler[self.in_packet.message_number](self)
            self._msg_num_to_handler[pd_packet.message_number](self)
        except (KeyError):
           self.default_handler()
         
        self.set_terminator(self.in_packet.header_length())    
        self.in_packet = None
        self.received_data = ''
        self.process_data = self._process_header


