"""FASIT Packet"""
import dpkt

# FIXME - is this correct
FASIT_ICD_VERSION_WHOLE             = 1
FASIT_ICD_VERSION_FRACT             = 1

# these should be moved
#FASIT_TC_IP_ADDRESS                 = 'localhost'
#FASIT_TC_IP_PORT                    = 4000

FASIT_DEV_DEF_REQUEST               = 100

FASIT_count = 0

class FasitPacket(dpkt.Packet):
    __message_number__ = FASIT_DEV_DEF_REQUEST
    __hdr__ = (
        ('message_number', 'H', FASIT_DEV_DEF_REQUEST),
        ('icd_ver_number_whole', 'H', FASIT_ICD_VERSION_WHOLE),
        ('icd_ver_number_fract', 'H', FASIT_ICD_VERSION_FRACT),
        ('sequence_id', 'I', 0),
        ('reserved', 'I', 0),
        ('length', 'H', 0)
        )

    _msg_num_to_type = {}
    
    def header_length(self):
        return self.__hdr_len__
    
    def unpack(self, buf):
        dpkt.Packet.unpack(self, buf)
        if self.message_number != FASIT_DEV_DEF_REQUEST:
            try:
                self.data = self._msg_num_to_type[self.message_number](self.data)
                setattr(self, self.data.__class__.__name__.lower(), self.data)
            except (KeyError, dpkt.UnpackError):
                self.data = buf
                
    def __repr__(self):
        l = [ '%s=%r' % (k, getattr(self, k))
              for k in self.__hdr_defaults__ ]
        if self.data:
            l.append('data=%r' % self.data)
        return '%s(%s)' % (self.__class__.__name__, ', '.join(l))
            
    def __str__(self):
        self.length = self.__hdr_len__ + len(self.data)
        
        if not self.data:   
            self.message_number = self.__message_number__
        else:
            self.message_number = self.data.__message_number__
        return dpkt.Packet.__str__(self)   
    
    def __init__(self, *args, **kwargs):
        global FASIT_count
        FASIT_count += 1
        dpkt.Packet.__init__(self, *args, **kwargs)
        
    def __del__(self):
        global FASIT_count
        FASIT_count -= 1
        #print "FASIT count: %i" % FASIT_count
        
                 
       
        