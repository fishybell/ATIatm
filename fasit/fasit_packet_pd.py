"""FASIT Packet for Presentation Devices ICD Release 1.1"""

import dpkt
import fasit_packet

PD_EVENT_CMD                    = 2100
PD_EVENT_CMD_ACK                = 2101
PD_DEV_STATUS                   = 2102
PD_CONFIG_MUZZLE_FLASH          = 2110
PD_DEV_ID_AND_CAPS              = 2111
PD_MUZZLE_FLASH_STATUS          = 2112
PD_GPS_LOCATION                 = 2113
PD_CONFIG_MILES_SHOOTBACK       = 2114
PD_MILES_SHOOTBACK_STATUS       = 2115
PD_AUDIO_CMD                    = 2116

PD_TYPE_NONE                    = 0
PD_TYPE_SIT                     = 1
PD_TYPE_MIT                     = 2
PD_TYPE_SAT_LIGHT               = 3
PD_TYPE_SAT_HEAVY               = 4
PD_TYPE_MAT                     = 5

EVENT_CMD_NONE                  = 0
EVENT_CMD_RESERVED              = 1
EVENT_CMD_REQ_STATUS            = 2
EVENT_CMD_REQ_EXPOSE            = 3
EVENT_CMD_REQ_DEV_RESET         = 4
EVENT_CMD_REQ_MOVE              = 5
EVENT_CMD_CONFIG_HIT_SENSOR     = 6
EVENT_CMD_REQ_GPS_LOCATION      = 7

PD_CAP_NONE                     = 0
PD_CAP_MILES_SHOOTBACK          = 1
PD_CAP_MUZZLE_FLASH             = 2
PD_CAP_GPS                      = 4

PD_POWER_SHORE_POWER            = 0
PD_POWER_ALTERNATE              = 1

PD_FAULT_NORMAL                                     =  0
PD_FAULT_BOTH_LEFT_AND_RIGHT_LIMITS_ARE_ACTIVE      =  1
PD_FAULT_INVALID_DIRECTION_REQUESTED                =  2
PD_FAULT_INVALID_SPEED_REQUESTED                    =  3
PD_FAULT_SPEED_0_REQUESTED                          =  4
PD_FAULT_STOPPED_AT_RIGHT_LIMIT                     =  5
PD_FAULT_STOPPED_AT_LEFT_LIMIT                      =  6
PD_FAULT_STOPPED_BY_DISTANCE_ENCODER                =  7
PD_FAULT_EMERGENCY_STOP                             =  8
PD_FAULT_NO_MOVEMENT_DETECTED                       =  9
PD_FAULT_OVER_SPEED_DETECTED                        =  10
PD_FAULT_UNASSIGNED                                 =  11
PD_FAULT_WRONG_DIRECTION_DETECTED                   =  12
PD_FAULT_STOPPED_DUE_TO_STOP_COMMAND                =  13
PD_FAULT_LIFTER_STUCK_AT_LIMIT                      =  14
PD_FAULT_ACTUATION_WAS_NOT_COMPLETED                =  15
PD_FAULT_DID_NOT_LEAVE_CONCEAL_SWITCH               =  16
PD_FAULT_DID_NOT_LEAVE_EXPOSE_SWITCH                =  17
PD_FAULT_DID_NOT_REACH_EXPOSE_SWITCH                =  18
PD_FAULT_DID_NOT_REACH_CONCEAL_SWITCH               =  19
PD_FAULT_LOW_BATTERY_DETECTED                       =  20
PD_FAULT_ENGINE_HAS_STOPPED                         =  21
PD_FAULT_IR_AUGMENTATION_FAILURE                    =  22
PD_FAULT_AUDIO_SUBSYSTEM_FAULT                      =  23
PD_FAULT_MILES_SUBSYSTEM_FAULT                      =  24
PD_FAULT_THERMAL_SUBSYSTEM_FAULT                    =  25
PD_FAULT_HIT_SENSOR_DISCONNECTED                    =  26
PD_FAULT_INVALID_TARGET_TYPE                        =  27
PD_FAULT_INCORRECTLY_FORMATTED_RF_PACKET            =  28
PD_FAULT_INCORRECT_CHECKSUM                         =  29
PD_FAULT_UNSUPPORTED_COMMAND                        =  30
PD_FAULT_INVALID_EXCEPTION_CODE_RETURNED            =  31

PD_EXPOSURE_CONCEALED           = 0
PD_EXPOSURE_TRANSITION          = 45
PD_EXPOSURE_EXPOSED             = 90

PD_MOVE_STOP                    = 0
PD_MOVE_FORWARD                 = 1
PD_MOVE_REVERSE                 = 2

PD_HIT_ONOFF_OFF                = 0
PD_HIT_ONOFF_ON                 = 1
PD_HIT_ONOFF_ON_POS             = 2
PD_HIT_ONOFF_OFF_POS            = 3

PD_HIT_REACTION_FALL            = 0
PD_HIT_REACTION_KILL            = 1
PD_HIT_REACTION_STOP            = 2           
PD_HIT_REACTION_FALL_STOP       = 3
PD_HIT_REACTION_BOB             = 4

PD_HIT_MODE_NCHS                = 0
PD_HIT_MODE_SINGLE              = 1
PD_HIT_MODE_BURST               = 2

PD_MUZZLE_FLASH_MODE_SINGLE     = 0
PD_MUZZLE_FLASH_MODE_BURST      = 1

PD_AUDIO_CMD_STOP_ALL           = 0
PD_AUDIO_CMD_PLAY_TRACK         = 1
PD_AUDIO_CMD_STOP_TRACK         = 2
PD_AUDIO_CMD_SET_VOLUME         = 3

PD_AUDIO_MODE_ONCE              = 0
PD_AUDIO_MODE_REPEAT            = 1
PD_AUDIO_MODE_RANDOM            = 2

FASIT_count = 0

class FasitPacketPd(fasit_packet.FasitPacket):

    class EventCommand(dpkt.Packet):
        __message_number__ = PD_EVENT_CMD
        __hdr__ = (
            ('command_id', 'B', 0), 
            ('exposure', 'B', 0),
            ('aspect', 'h', 0),
            ('direction', 'H', 0),
            ('move', 'B', 0),
            ('speed', 'f', 0),
            ('hit_onoff', 'B', 0),
            ('hit_count', 'H', 0),
            ('hit_reaction', 'B', 0),
            ('hits_to_kill', 'H', 0),
            ('hit_sensitivity', 'H', 0),
            ('hit_mode', 'B', 0),
            ('hit_burst_separation', 'H', 0)
            )
        
    class EventCommandAck(dpkt.Packet):
        __message_number__ = PD_EVENT_CMD_ACK
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('ack_resp', 'c', 'F')
            )

    class DeviceIdAndCapabilities(dpkt.Packet):
        __message_number__ = PD_DEV_ID_AND_CAPS 
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('device_id', 'Q', 0),              # make sure the Q (C long long) is supported on embedded platform
            ('device_capabilities', 'B', 0)
            )
        
    class DeviceStatus(dpkt.Packet):
        __message_number__ = PD_DEV_STATUS
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('power_status', 'B', 0), 
            ('oem_fault_code', 'H', 0), 
            ('exposure', 'B', 0),
            ('aspect', 'h', 0),
            ('direction', 'H', 0),
            ('move', 'B', 0),
            ('speed', 'f', 0),
            ('track_position', 'H', 0), 
            ('device_type', 'B', 0), 
            ('hit_count', 'H', 0),
            ('hit_onoff', 'B', 0),
            ('hit_reaction', 'B', 0),
            ('hits_to_kill', 'H', 0),
            ('hit_sensitivity', 'H', 0),
            ('hit_mode', 'B', 0),
            ('hit_burst_separation', 'H', 0)
            )
        
    class ConfigureMilesShootback(dpkt.Packet):
        __message_number__ = PD_CONFIG_MILES_SHOOTBACK 
        __hdr__ = (
            ('basic_miles_code', 'B', 0),
            ('ammo_type', 'B', 0),
            ('player_id', 'H', 0),
            ('fire_delay', 'B', 0)
            )
        
    class MilesShootbackStatus(dpkt.Packet):
        __message_number__ = PD_MILES_SHOOTBACK_STATUS 
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('basic_miles_code', 'B', 0),             
            ('ammo_type', 'B', 0),
            ('player_id', 'H', 0),
            ('fire_delay', 'B', 0)
            )
        
    class ConfigureMuzzleFlash(dpkt.Packet):
        __message_number__ = PD_CONFIG_MUZZLE_FLASH 
        __hdr__ = (
            ('on_off', 'B', 0),
            ('mode', 'B', 0),
            ('initial_delay', 'B', 0),
            ('repeat_delay', 'B', 0)
            )

    class MuzzleFlashStatus(dpkt.Packet):
        __message_number__ = PD_MUZZLE_FLASH_STATUS 
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('on_off', 'B', 0),
            ('mode', 'B', 0),
            ('initial_delay', 'B', 0),
            ('repeat_delay', 'B', 0)
            ) 
        
    class GpsLocation(dpkt.Packet):
        __message_number__ = PD_GPS_LOCATION 
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('gps_fom', 'B', 0),
            ('integral_latitude', 'H', 0),
            ('fractional_latitude', 'I', 0),
            ('integral_longitude', 'H', 0),
            ('fractional_longitude', 'I', 0)
            )    
        
    class AudioCommand(dpkt.Packet):
        __message_number__ = PD_AUDIO_CMD 
        __hdr__ = (
            ('function_code', 'B', 0),
            ('track_number', 'B', 1),
            ('volume', 'B', 0),
            ('play_mode', 'B', 0)
            )    
        
    _msg_num_to_type = { 
            PD_EVENT_CMD                    :EventCommand,
            PD_EVENT_CMD_ACK                :EventCommandAck,
            PD_DEV_STATUS                   :DeviceStatus,
            PD_CONFIG_MUZZLE_FLASH          :ConfigureMuzzleFlash,
            PD_DEV_ID_AND_CAPS              :DeviceIdAndCapabilities,
            PD_MUZZLE_FLASH_STATUS          :MuzzleFlashStatus,
            PD_GPS_LOCATION                 :GpsLocation,
            PD_CONFIG_MILES_SHOOTBACK       :ConfigureMilesShootback,
            PD_MILES_SHOOTBACK_STATUS       :MilesShootbackStatus,
            PD_AUDIO_CMD                    :AudioCommand
            }

