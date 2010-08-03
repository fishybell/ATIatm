"""FASIT Packet for Presentation Devices ICD Release 2.0"""

import fasit_dpkt
import fasit_packet

FASIT_PD_SIMPLE_CMD                 = 3100
FASIT_PD_CONFIG_HIT_SENSOR          = 3101
FASIT_PD_CONFIG_MILES_SHOOTBACK     = 3102
FASIT_PD_CONFIG_MUZZLE_FLASH        = 3103
FASIT_PD_EVENT_CMD_ACK              = 3110
FASIT_PD_DEV_ID_AND_CAPS            = 3111
FASIT_PD_GENERAL_STATUS             = 3112
FASIT_PD_MOVEMENT_STATUS            = 3113
FASIT_PD_CONFIG_HIT_SENSOR_STATUS   = 3114
FASIT_PD_HIT_STATUS                 = 3115
FASIT_PD_MILES_SHOOTBACK_STATUS     = 3116
FASIT_PD_MUZZLE_FLASH_STATUS        = 3117
FASIT_PD_GPS_LOCATION               = 3118
FASIT_PD_VENDOR_SPECIFIC            = 3150

FASIT_PD_TYPE_SIT                   = 1
FASIT_PD_TYPE_MIT                   = 2
FASIT_PD_TYPE_SAT                   = 3
FASIT_PD_TYPE_MAT                   = 4
FASIT_PD_TYPE_HUT                   = 5
FASIT_PD_TYPE_PREC_TARGET           = 6

FASIT_PD_VENDOR_ID_LOCKHEED_MARTIN  = 1
FASIT_PD_VENDOR_ID_SAAB             = 2
FASIT_PD_VENDOR_ID_MDS_CASWELL      = 3
FASIT_PD_VENDOR_ID_ACTION_TARGET    = 4
FASIT_PD_VENDOR_ID_ATS              = 4
FASIT_PD_VENDOR_ID_THEISSEN         = 5
FASIT_PD_VENDOR_ID_POLYTRONIC       = 6

SIMPLE_CMD_NONE                        = 0
SIMPLE_CMD_EXPOSE                      = 1
SIMPLE_CMD_CONCEAL                     = 2
SIMPLE_CMD_REQ_FULL_STATUS             = 3
SIMPLE_CMD_REQ_GENERAL_STATUS          = 4
SIMPLE_CMD_REQ_HIT_SENSOR_STATUS       = 5
SIMPLE_CMD_REQ_HIT_COUNT_STATUS        = 6
SIMPLE_CMD_REQ_MOVEMENT_STATUS         = 7
SIMPLE_CMD_REQ_MILES_SHOOTBACK_STATUS  = 8
SIMPLE_CMD_REQ_MFS_STATUS              = 9
SIMPLE_CMD_ASPECT_SET                  = 10
SIMPLE_CMD_MFS_ON                      = 11
SIMPLE_CMD_MFS_OFF                     = 12
SIMPLE_CMD_THERMAL_ON                  = 13
SIMPLE_CMD_THERMAL_OFF                 = 14
SIMPLE_CMD_MOVE_FORWARD                = 15
SIMPLE_CMD_MOVE_REVERSE                = 16
SIMPLE_CMD_MOVE_STOP                   = 17
SIMPLE_CMD_HIT_COUNT_RESET             = 18
SIMPLE_CMD_HIT_COUNT_INCREMENT         = 19
SIMPLE_CMD_HIT_COUNT_SET_KILL          = 20
SIMPLE_CMD_REQ_GPS_LOCATION            = 21
SIMPLE_CMD_AUX_DEV_ON_1                = 22
SIMPLE_CMD_AUX_DEV_OFF_1               = 23
SIMPLE_CMD_AUX_DEV_ON_2                = 24
SIMPLE_CMD_AUX_DEV_OFF_2               = 25
SIMPLE_CMD_AUX_DEV_ON_3                = 26
SIMPLE_CMD_AUX_DEV_OFF_3               = 27

FASIT_count = 0

class FasitPacketPd(fasit_packet.FasitPacket):
    
    class Simple(fasit_dpkt.Packet):
        __message_number__ = FASIT_PD_SIMPLE_CMD
        __hdr__ = (
            ('command_id', 'B', 0), 
            ('command_data', 'H', 0)
            )

    class ConfigureHitSensor(fasit_dpkt.Packet):
        __message_number__ = FASIT_PD_CONFIG_HIT_SENSOR
        __hdr__ = (
            ('on_off', '?', 0),
            ('reaction', 'B', 0),
            ('hits_to_kill', 'B', 0),
            ('sensitivity', 'B', 0),
            ('mode', 'B', 0),
            ('burst_separation', 'H', 0)
            )

    class EventCommandAck(fasit_dpkt.Packet):
        __message_number__ = FASIT_PD_EVENT_CMD_ACK
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('ack_resp', 'c', 'F')
            )

    class DeviceIdAndCapabilities(fasit_dpkt.Packet):
        __message_number__ = FASIT_PD_DEV_ID_AND_CAPS 
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('device_id', 'Q', 0),              # make sure the Q (C long long) is supported on embedded platform
            ('device_type', 'B', 0),
            ('vendor_id', 'I', 0),
            ('fw_version_major', 'B', 0),
            ('fw_version_minor', 'B', 0),
            ('device_capabilities', 'B', 0)
            )

    class GeneralStatus(fasit_dpkt.Packet):
        __message_number__ = FASIT_PD_GENERAL_STATUS 
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('power_status', '?', 0),   
            ('thermal_on_off', '?', 0),
            ('aux_dev_1_on_off', '?', 0),
            ('aux_dev_2_on_off', '?', 0),
            ('aux_dev_3_on_off', '?', 0),
            ('vendor_status', 'B', 0),
            ('oem_fault_field', 'B', 0),
            ('exposure', 'B', 0),
            ('aspect', 'B', 0)
            )

    class MovementStatus(fasit_dpkt.Packet):
        __message_number__ = FASIT_PD_MOVEMENT_STATUS 
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('move_Setting', 'B', 0),   
            ('speed_mph', 'B', 0),
            ('track_position', 'H', 0)
            )

    class ConfigureHitSensorStatus(fasit_dpkt.Packet):
        __message_number__ = FASIT_PD_CONFIG_HIT_SENSOR_STATUS 
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('on_off', '?', 0),
            ('reaction', 'B', 0),
            ('hits_to_kill', 'B', 0),
            ('sensitivity', 'B', 0),
            ('mode', 'B', 0),
            ('burst_separation', 'H', 0)
            )

    class HitStatus(fasit_dpkt.Packet):
        __message_number__ = FASIT_PD_HIT_STATUS 
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('hit_count', 'B', 0),
            ('hit_location', 'B', 0)
            )

    class ConfigureMilesShootback(fasit_dpkt.Packet):
        __message_number__ = FASIT_PD_CONFIG_MILES_SHOOTBACK 
        __hdr__ = (
            ('on_off', '?', 0),
            ('basic_miles_code', 'B', 0),
            ('ammo_type', 'B', 0),
            ('sensitivity', 'B', 0),
            ('player_id', 'H', 0),
            ('fire_delay', 'B', 0)
            )
        
    class MilesShootbackStatus(fasit_dpkt.Packet):
        __message_number__ = FASIT_PD_MILES_SHOOTBACK_STATUS 
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('on_off', '?', 0),
            ('basic_miles_code', 'B', 0),             
            ('ammo_type', 'B', 0),
            ('sensitivity', 'B', 0),
            ('player_id', 'H', 0),
            ('fire_delay', 'B', 0)
            )

    class ConfigureMuzzleFlash(fasit_dpkt.Packet):
        __message_number__ = FASIT_PD_CONFIG_MUZZLE_FLASH 
        __hdr__ = (
            ('on_off', '?', 0),
            ('mode', 'B', 0),
            ('repeat_delay', 'B', 0),
            ('flashes_per_burst', 'B', 0)
            )
        
    class MuzzleFlashStatus(fasit_dpkt.Packet):
        __message_number__ = FASIT_PD_MUZZLE_FLASH_STATUS 
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('on_off', '?', 0),
            ('mode', 'B', 0),
            ('repeat_delay', 'B', 0),
            ('flashes_per_burst', 'B', 0)
            )
        
    class GpsLocation(fasit_dpkt.Packet):
        __message_number__ = FASIT_PD_GPS_LOCATION 
        __hdr__ = (
            ('resp_message_number', 'H', 0),
            ('resp_sequence_id', 'I', 0),
            ('gps_fom', 'B', 0),
            ('integral_latitude', 'H', 0),
            ('fractional_latitude', 'I', 0),
            ('integral_longitude', 'H', 0),
            ('fractional_longitude', 'I', 0)
            )

    class VendorSpecific(fasit_dpkt.Packet):
        __message_number__ = FASIT_PD_VENDOR_SPECIFIC 
        __hdr__ = (
            ('vendor_id', 'H', 0),
            ('place_holder', 'H', 0)
            )

    _msg_num_to_type = { 
            FASIT_PD_SIMPLE_CMD                 :Simple,
            FASIT_PD_CONFIG_HIT_SENSOR          :ConfigureHitSensor, 
            FASIT_PD_CONFIG_MILES_SHOOTBACK     :ConfigureMilesShootback,
            FASIT_PD_CONFIG_MUZZLE_FLASH        :ConfigureMuzzleFlash,
            FASIT_PD_EVENT_CMD_ACK              :EventCommandAck,
            FASIT_PD_DEV_ID_AND_CAPS            :DeviceIdAndCapabilities,
            FASIT_PD_GENERAL_STATUS             :GeneralStatus,
            FASIT_PD_MOVEMENT_STATUS            :MovementStatus,
            FASIT_PD_CONFIG_HIT_SENSOR_STATUS   :ConfigureHitSensorStatus,
            FASIT_PD_HIT_STATUS                 :HitStatus, 
            FASIT_PD_MILES_SHOOTBACK_STATUS     :MilesShootbackStatus,
            FASIT_PD_MUZZLE_FLASH_STATUS        :MuzzleFlashStatus,
            FASIT_PD_GPS_LOCATION               :GpsLocation,
            FASIT_PD_VENDOR_SPECIFIC            :VendorSpecific
            }
