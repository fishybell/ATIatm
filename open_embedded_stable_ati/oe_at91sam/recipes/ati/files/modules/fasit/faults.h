#ifndef _FAULTS_H_
#define _FAULTS_H_

// PD error codes
enum {
   ERR_normal,
   ERR_both_limits_active,
   ERR_invalid_direction_req,
   ERR_invalid_speed_req,
   ERR_speed_zero_req,
   ERR_stop_right_limit,
   ERR_stop_left_limit,
   ERR_stop_by_distance,
   ERR_emergency_stop,
   ERR_no_movement,
   ERR_over_speed,
   ERR_unassigned,
   ERR_wrong_direction,
   ERR_stop,
   ERR_lifter_stuck_at_limit,
   ERR_actuation_not_complete,
   ERR_not_leave_conceal,
   ERR_not_leave_expose,
   ERR_not_reach_expose,
   ERR_not_reach_conceal,
   ERR_low_battery,
   ERR_engine_stop,
   ERR_IR_failure,
   ERR_audio_failure,
   ERR_miles_failure,
   ERR_thermal_failure,
   ERR_hit_sensor_failure,
   ERR_invalid_target_type,
   ERR_bad_RF_packet,
   ERR_bad_checksum,
   ERR_unsupported_command,
   ERR_invalid_exception,
   ERR_critical_battery=188,
};

#endif

