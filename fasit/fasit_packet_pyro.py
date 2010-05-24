"""FASIT Packet for Pyrotechnic Devices ICD Release 2.1"""

import dpkt
import fasit_packet

PYRO_CMD                        = 2000
PYRO_CMD_ACK                    = 2004
PYRO_DEV_ID_AND_CAPS            = 2005
PYRO_DEV_STATUS                 = 2006

EVENT_CMD_NONE                  = 0
EVENT_CMD_RESERVED              = 1
EVENT_CMD_REQ_STATUS            = 2
EVENT_CMD_SET_FIRE              = 3
EVENT_CMD_FIRE                  = 4

FASIT_count = 0

class FasitPacketPyro(fasit_packet.FasitPacket):

    class Command(dpkt.Packet):
        __message_number__ = PYRO_EVENT_CMD
        __hdr__ = (
            ('command_id', 'B', 0), 
            ('fire_zone', 'H', 0)
            )
        
    class CommandAck(dpkt.Packet):
        __message_number__ = PYRO_EVENT_CMD_ACK
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('ack_resp', 'c', 'F')
            )

    class DeviceIdAndCapabilities(dpkt.Packet):
        __message_number__ = PYRO_DEV_ID_AND_CAPS 
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('device_id', 'Q', 0),              # make sure the Q (C long long) is supported on embedded platform
            ('device_capabilities', 'B', 0)
            )
        
    class DeviceStatus(dpkt.Packet):
        __message_number__ = PYRO_DEV_STATUS
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('power_status', 'B', 0), 
            ('oem_fault_code', 'H', 0), 
            ('zones', 'H', 0)
            )
        
    _msg_num_to_type = { 
            PYRO_CMD                        :Command,
            PYRO_CMD_ACK                    :CommandAck,
            PYRO_DEV_STATUS                 :DeviceStatus,
            PYRO_DEV_ID_AND_CAPS            :DeviceIdAndCapabilities
            }

