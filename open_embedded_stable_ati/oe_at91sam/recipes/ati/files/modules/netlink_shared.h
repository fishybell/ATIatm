#ifndef NETLINK_SHARED_H
#define NETLINK_SHARED_H

#ifdef NETLINK_USER_H
typedef signed int s32;
typedef signed short s16;
typedef signed char s8;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
#endif

#define ATI_GROUP 599

/* generic policy with a single string attribute */
enum {
    GEN_STRING_A_UNSPEC,
    GEN_STRING_A_MSG,
    __GEN_STRING_A_MAX,
};
#define GEN_STRING_A_MAX (__GEN_STRING_A_MAX - 1)
static struct nla_policy generic_string_policy[GEN_STRING_A_MAX + 1] = {
#ifdef NETLINK_USER_H
    {}, {NLA_STRING, 0, 0},
#else
    [GEN_STRING_A_MSG] = { .type = NLA_STRING },
#endif
};
/* generic policy with a single 16-bit integer attribute */
enum {
    GEN_INT16_A_UNSPEC,
    GEN_INT16_A_MSG,
    __GEN_INT16_A_MAX,
};
#define GEN_INT16_A_MAX (__GEN_INT16_A_MAX - 1)
static struct nla_policy generic_int16_policy[GEN_INT16_A_MAX + 1] = {
#ifdef NETLINK_USER_H
    {}, {NLA_U16, 0, 0},
#else
    [GEN_INT16_A_MSG] = { .type = NLA_U16 },
#endif
};
/* generic policy with a single 8-bit integer attribute */
enum {
    GEN_INT8_A_UNSPEC,
    GEN_INT8_A_MSG,
    __GEN_INT8_A_MAX,
};
#define GEN_INT8_A_MAX (__GEN_INT8_A_MAX - 1)
static struct nla_policy generic_int8_policy[GEN_INT8_A_MAX + 1] = {
#ifdef NETLINK_USER_H
    {}, {NLA_U8, 0, 0},
#else
    [GEN_INT8_A_MSG] = { .type = NLA_U8 },
#endif
};

/* specific policy for hit sensor calibration */
typedef struct hit_on_line {
    u8 hits:4;                           /* explained below in overwrite enum */
    u8 line:4;                           /* for ttmt mover we need which hit line we are configuring 0-SAT, 1-front, 2-back, 3-engine */
} hit_on_line_t;

/* specific policy for hit sensor calibration */
typedef struct hit_calibration {
    u32 seperation __attribute__ ((packed));   /* seperation calibration value (in milliseconds) */
    u32 sensitivity __attribute__ ((packed));  /* sensitivity calibration value (lower value for less sensitive) */
    u16 blank_time:10 __attribute__ ((packed));   /* blank time from start of exposure (in tenths of seconds) */
    u16 enable_on:3 __attribute__ ((packed));   /* blank when commanded (enumerated below) */
    u16 after_kill:3 __attribute__ ((packed));  /* after kill: 0 stay down, 1 bob, 2 bob/stop, 3 stop */
    u8 hits_to_kill;                         /* number of hits required to kill (0 for infinity) */
    u8 hits_to_bob;                         /* number of hits required to bob (0 is same as 1) */
    u8 bob_type:2;                             /* Type of bob: 0 bob at kill, 1 bob at hit */ 
    u8 type:2;                                 /* 0 for NCHS, 1 for mechanical (single-fire), 2 for mechanical (burst-fire), 3 for MILES */
    u8 invert:2;                               /* invert hit sensor input: 0 for no, 1 for yes, 2 for auto (not implimented) */
    u8 set:4;                                  /* explained below in overwrite enum */
    u8 line:4;                           /* for ttmt mover we need which hit line we are configuring 0-SAT, 1-front, 2-back, 3-engine */
} hit_calibration_t;
enum {
    BLANK_ALWAYS,           /* hit sensor disabled blank */
    ENABLE_ALWAYS,       /* enable full-time (even when concealed) */
    ENABLE_AT_POSITION,  /* enable when reach next position (don't change now) */
    DISABLE_AT_POSITION, /* disable when reach next position (don't change now) */
    BLANK_ON_CONCEALED,    /* blank when fully concealed (enabled most of the time) */
};
enum {
    HIT_OVERWRITE_NONE,  /* overwrite nothing (gets reply with current values) */
    HIT_OVERWRITE_ALL,   /* overwrites every value */
    HIT_OVERWRITE_CAL,   /* overwrites calibration values (sensitivity, seperation, blank_time) */
    HIT_OVERWRITE_OTHER, /* overwrites non-calibration values (type, etc.) */
    HIT_OVERWRITE_TYPE,  /* overwrites type and invert values */
    HIT_OVERWRITE_KILL,  /* overwrites hits_to_kill and after_kill values */
    HIT_GET_CAL,         /* overwrites nothing (gets calibration values) */
    HIT_GET_TYPE,        /* overwrites nothing (gets type and invert values) */
    HIT_GET_KILL,        /* overwrites nothing (gets hits_to_kill and after_kill value) */
};
enum {
    HIT_A_UNSPEC,
    HIT_A_MSG, /* hit calibration structure */
    __HIT_A_MAX,
};
#define HIT_A_MAX (__HIT_A_MAX - 1)
static struct nla_policy hit_calibration_policy[HIT_A_MAX + 1] = {
#ifdef NETLINK_USER_H
    {}, { NLA_UNSPEC, sizeof(struct hit_calibration), sizeof(struct hit_calibration) },
#else
    [HIT_A_MSG] = { .len = sizeof(struct hit_calibration) },
#endif
};
enum {
    HIT_M_UNSPEC,
    HIT_M_MSG, /* mover hit structure */
    __HIT_M_MAX,
};
#define HIT_M_MAX (__HIT_M_MAX - 1)
static struct nla_policy hit_on_line_policy[HIT_M_MAX + 1] = {
#ifdef NETLINK_USER_H
    {}, { NLA_UNSPEC, sizeof(struct hit_on_line), sizeof(struct hit_on_line) },
#else
    [HIT_M_MSG] = { .len = sizeof(struct hit_on_line) },
#endif
};
/* specific policy for bit button events */
typedef struct bit_event {
    u16 bit_type __attribute__ ((packed)); /* type of bit button used (enum below) */
    u16 is_on __attribute__ ((packed));    /* 1 for the current state being on, 0 for off */
} bit_event_t;
enum {
    BIT_TEST,       /* test button */
    BIT_MOVE_FWD,   /* move forward */
    BIT_MOVE_REV,   /* move reverse */
    BIT_MOVE_STOP,  /* move stop */
    BIT_MODE,       /* mode button (is_on contains new mode number) */
    BIT_KNOB,       /* knob twisted (is_on contains number) */
    BIT_KNOB_REQ,   /* request knob */
    BIT_MODE_REQ,   /* request mode */
    BIT_LONG_PRESS, /* test button, long press */
    BIT_GOTO_DOCK,  /* move the mover to the dock */
};
enum {
    BIT_A_UNSPEC,
    BIT_A_MSG, /* bit event structure */
    __BIT_A_MAX,
};
#define BIT_A_MAX (__BIT_A_MAX - 1)
static struct nla_policy bit_event_policy[BIT_A_MAX + 1] = {
#ifdef NETLINK_USER_H
    {}, { NLA_UNSPEC, sizeof(struct bit_event), sizeof(struct bit_event) },
#else
    [BIT_A_MSG] = { .len = sizeof(struct bit_event) },
#endif
};

/* specific policy for accessory configuration */
typedef struct accessory_conf {
    u8 acc_type:6;   /* type of accessory used (enum below) */
    u8 request:1;    /* request all data for this accessory (on reply, on_now will indicate current status (if available), exists will indicate existence of accessory */
    u8 exists:1;     /* 1 for exists, not used except for requests */
    u8 on_now:2;     /* 1 for activate normal, 2 for activate immediate */
    u8 on_exp:3;     /* 1 for active when fully exposed, 2 for active when partially exposed and fully exposed, 3 for active only while exposing/concealing */
    u8 pad1:3;
    u8 on_hit:2;     /* 1 for activate on hit, 2 for deactivate on hit */
    u8 on_kill:2;    /* 1 for activate on kill, 2 for deactivate on kill */
    u8 pad2:4;
    u16 on_time __attribute__ ((packed));     /* time on (in milliseconds, 0 for forever) */
    u16 off_time __attribute__ ((packed));    /* time off (in milliseconds, 0 for forever) */
    u8 start_delay;  /* time to delay before activation (in half-seconds) */
    u8 repeat_delay; /* time to delay before repeat (in half-seconds) */
    u16 repeat:6 __attribute__ ((packed));    /* repeat count (0 for no repeat, 63 for forever) */
    u16 ex_data1:10 __attribute__ ((packed)); /* extra data specific to the accessory type */
    u8 ex_data2;     /* more extra data specific to the accessory type */
    u8 ex_data3;     /* even more extra data specific to the accessory type */
} accessory_conf_t;
enum {
    ACC_NES_MGL,      /* Night Effects Simulator, Moon Glow light */
    ACC_NES_PHI,            /* Night Effects Simulator, Positive Hit Indicator light */
    ACC_NES_MFS,            /* Night Effects Simulator, Muzzle Flash Simulator light : ex_data1 = flash type, ex_data2 = burst count */
    ACC_SES,                /* Sound Effects Simulator : ex_data1 = track #, ex_data2 = record length (in seconds, 0 for play) */
    ACC_SMOKE,              /* Smoke generator : ex_data1 = smoke # */
    ACC_THERMAL,            /* Thermal device : ex_data1 = thermal # */
    ACC_MILES_SDH,          /* MILES, Shootback Device Holder : ex_data1 = Player ID, ex_data2 = MILES Code, ex_data3 = Ammo Type*/
    ACC_BES_ENABLE,      // BES enable
    ACC_BES_TRIGGER_1,      // BES trigger
    ACC_BES_TRIGGER_2,      // BES trigger
    ACC_BES_TRIGGER_3,      // BES trigger
    ACC_BES_TRIGGER_4,      // BES trigger
    ACC_THERMAL_PULSE,      // Thermal pulse for MATs
    ACC_INTERNAL,           /* Internal type for other outputs: don't use */
};
enum {
    ACC_A_UNSPEC,
    ACC_A_MSG, /* accesory conf structure */
    __ACC_A_MAX,
};
#define ACC_A_MAX (__ACC_A_MAX - 1)
static struct nla_policy accessory_conf_policy[ACC_A_MAX + 1] = {
#ifdef NETLINK_USER_H
    {}, { NLA_UNSPEC, sizeof(struct accessory_conf), sizeof(struct accessory_conf) },
#else
    [ACC_A_MSG] = { .len = sizeof(struct accessory_conf) },
#endif
};

/* specific policy for gps configuration */
typedef struct gps_conf {
    u32 fom;                             /* GPS Field of Merit */
    u16 intLat __attribute__ ((packed)); /* Integral Latitude */
    u32 fraLat;                          /* Fractional Latitude */
    u16 intLon __attribute__ ((packed)); /* Integral Longitude */
    u32 fraLon;                          /* Fractional Longitude */
} gps_t;
enum {
    GPS_A_UNSPEC,
    GPS_A_MSG, /* gps conf structure */
    __GPS_A_MAX,
};
#define GPS_A_MAX (__GPS_A_MAX - 1)
static struct nla_policy gps_conf_policy[GPS_A_MAX + 1] = {
#ifdef NETLINK_USER_H
    {}, { NLA_UNSPEC, sizeof(struct gps_conf), sizeof(struct gps_conf) },
#else
    [GPS_A_MSG] = { .len = sizeof(struct gps_conf) },
#endif
};

/* specific policy for command event */
typedef struct cmd_event {
    u32 role:8;         /* Which "role" should tackle this message */
    u32 cmd:8;          /* The netlink command to send */
    u32 payload_size:8; /* The payload size of the netlink command (0-16 are valid) */
    u32 attribute:8;    /* The payload attribute parameter */
    u8  payload[16];    /* Payload data */
} cmd_event_t;
enum {
    CMD_EVENT_A_UNSPEC,
    CMD_EVENT_A_MSG, /* command event structure */
    __CMD_EVENT_A_MAX,
};
#define CMD_EVENT_A_MAX (__CMD_EVENT_A_MAX - 1)
static struct nla_policy cmd_event_policy[CMD_EVENT_A_MAX + 1] = {
#ifdef NETLINK_USER_H
    {}, { NLA_UNSPEC, sizeof(struct cmd_event), sizeof(struct cmd_event) },
#else
    [CMD_EVENT_A_MSG] = { .len = sizeof(struct cmd_event) },
#endif
};

/* this value needs to be the highest among MAX attribute values (so far, mine are all the same) */
#define NL_A_MAX GPS_A_MAX

/* commands: enumeration of all commands (functions), 
 * used by userspace application to identify command to be executed
 * used by kernel modules to determine command handler
 * comment format: description (type) (policy)
 */
enum {
    NL_C_UNSPEC,
    NL_C_FAILURE,    /* failure message (reply) (generic string) */
    NL_C_BATTERY,    /* battery status as percentage (request/reply) (generic 8-bit int) */
    NL_C_EXPOSE,     /* expose/conceal (command/reply) (generic 8-bit int) */
    NL_C_MOVE,       /* move as mph (command/reply) (generic 16-bit int) */
    NL_C_POSITION,   /* position in feet from home (request/reply) (generic 16-bit int) */
    NL_C_STOP,       /* stop (command/reply) (generic 8-bit int) */
    NL_C_HITS,       /* hit count (request/reply) (generic 8-bit int) */
    NL_C_HIT_LOG,    /* hit count (request/reply) (generic string) */
    NL_C_HIT_CAL,    /* calibrate hit sensor (command/reply) (hit calibrate structure) */
    NL_C_BIT,        /* bit button event (broadcast) (bit event structure) */
    NL_C_ACCESSORY,  /* configure accesories (command/reply) (accessory structure) */
    NL_C_GPS,        /* gps status (request/reply) (gps structure) */
    NL_C_EVENT,      /* mover/lifter event (command/reply) (generic 8-bit int) */
    NL_C_SLEEP,      /* sleep/wake command (command) (generic 8-bit int) */
    NL_C_DMSG,       /* debug message (reply) (generic string) */
    NL_C_CMD_EVENT,  /* command event (command) (command event structure) */
    NL_C_SCENARIO,   /* run scenario message (reply) (generic string) */
    NL_C_EVENT_REF,  /* reflected event (command) (generic 8-bit int) */
    NL_C_CONTINUOUS,       /* move continuous as mph (command/reply) (generic 16-bit int) */
    NL_C_MOVEAWAY,       /* move continuous as mph (command/reply) (generic 16-bit int) */
    NL_C_GOHOME,       /* go home, used for - abort, pause, restart scenarios */
    NL_C_HIT_CAL_MOVER,    /* calibrate hit sensor (command/reply) (hit calibrate structure) */
    NL_C_HITS_MOVER,    /* mover hits message) */
    NL_C_COAST,    /* ttmt needs to "coast" to a stop */
    NL_C_FAULT,      /* fault event (reply) (generic 8-bit int) */
    __NL_C_MAX,
};
#define NL_C_MAX (__NL_C_MAX - 1)

typedef struct nl_attr_size {
    int cmd;
    int size;
    struct nla_policy *policy;
} nl_attr_size_t;
static nl_attr_size_t nl_attr_sizes[] = {
   {NL_C_UNSPEC, 0, NULL},
   {NL_C_FAILURE, -1, generic_string_policy},
   {NL_C_BATTERY, sizeof(u8), generic_int8_policy},
   {NL_C_EXPOSE, sizeof(u8), generic_int8_policy},
   {NL_C_MOVE, sizeof(u16), generic_int16_policy},
   {NL_C_POSITION, sizeof(u16), generic_int16_policy},
   {NL_C_STOP, sizeof(u8), generic_int8_policy},
   {NL_C_HITS, sizeof(u8), generic_int8_policy},
   {NL_C_HIT_LOG, -1, generic_string_policy},
   {NL_C_HIT_CAL, sizeof(hit_calibration_t), hit_calibration_policy},
   {NL_C_BIT, sizeof(bit_event_t), bit_event_policy},
   {NL_C_ACCESSORY, sizeof(accessory_conf_t), accessory_conf_policy},
   {NL_C_GPS, sizeof(gps_t), gps_conf_policy},
   {NL_C_EVENT, sizeof(u8), generic_int8_policy},
   {NL_C_SLEEP, sizeof(u8), generic_int8_policy},
   {NL_C_DMSG, -1, generic_string_policy},
   {NL_C_CMD_EVENT, sizeof(cmd_event_t), cmd_event_policy},
   {NL_C_SCENARIO, -1, generic_string_policy},
   {NL_C_EVENT_REF, sizeof(u8), generic_int8_policy},
   {NL_C_CONTINUOUS, sizeof(u16), generic_int16_policy},
   {NL_C_MOVEAWAY, sizeof(u16), generic_int16_policy},
   {NL_C_GOHOME, sizeof(u8), generic_int8_policy},
   {NL_C_HIT_CAL_MOVER, sizeof(hit_calibration_t), hit_calibration_policy},
   {NL_C_HITS_MOVER, sizeof(hit_on_line_t), hit_on_line_policy},
   {NL_C_COAST, sizeof(u8), generic_int8_policy},
   {NL_C_FAULT, sizeof(u8), generic_int8_policy},
   {__NL_C_MAX, 0, NULL},
};

enum {
    CMD_PAUSE,       /* pause mover */
    CMD_ABORT,   /* abort mover - send home */
    CMD_RESTART,   /* restart mover - send home */
};

#define CONCEAL 0
#define EXPOSE 1
#define LIFTING 2
#define TOGGLE 3
#define EXPOSURE_REQ 255

#define HIT_REQ 255

#define VELOCITY_STOP 65535
#define VELOCITY_COAST 32768
#define VELOCITY_REQ 0

#define BATTERY_REQUEST 1
#define BATTERY_SHUTDOWN 0
// defined battery percentage stop-points
#define BAT_NORMAL   90
#define BAT_LOW      20
#define BAT_CRIT     5
#define BAT_INVALID  1
#define BAT_HALT     0

#define SLEEP_COMMAND 0
#define WAKE_COMMAND 1
#define SLEEP_REQUEST 2
#define DOCK_COMMAND 3

#endif
