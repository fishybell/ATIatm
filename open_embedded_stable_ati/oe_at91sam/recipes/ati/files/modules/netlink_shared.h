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
typedef struct hit_calibration {
    u16 lower __attribute__ ((packed)); /* lower calibration value */
    u16 upper __attribute__ ((packed)); /* upper calibration value */
    u16 burst __attribute__ ((packed)); /* burst seperation value (0 for NCHS, 1 for single) */
    u8  hit_to_fall;                    /* number of hits required to fall (0 for infinity) */
    u8  set;                            /* explained below */
} hit_calibration_t;
enum {
    HIT_OVERWRITE_NONE,  /* overwrite nothing (gets reply with current values) */
    HIT_OVERWRITE_ALL,   /* overwrites every value */
    HIT_OVERWRITE_CAL,   /* overwrites calibration values (upper, lower) */
    HIT_OVERWRITE_OTHER, /* overwrites non-calibration values (burst, etc.) */
    HIT_OVERWRITE_BURST, /* overwrites burst value only */
    HIT_OVERWRITE_HITS,  /* overwrites hit_to_fall value only */
    HIT_GET_CAL,         /* overwrites nothing (gets calibration values) */
    HIT_GET_BURST,       /* overwrites nothing (gets burst value) */
    HIT_GET_HITS,        /* overwrites nothing (gets hit_to_fall value) */
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
    u8 acc_type:6;	/* type of accessory used (enum below) */
    u8 request:1;	/* request all data for this accessory (on reply, on_now will indicate current status (if available), exists will indicate existence of accessory */
    u8 exists:1;	/* 1 for exists, not used except for requests */
    u8 on_now:2;	/* 1 for activate normal, 2 for activate immediate */
    u8 on_exp:2;	/* 1 for active when fully exposed, 2 for active when partially exposed and fully exposed, 3 for active only while exposing/concealing */
    u8 on_hit:2;	/* 1 for activate on hit, 2 for deactivate on hit */
    u8 on_kill:2;	/* 1 for activate on kill, 2 for deactivate on kill */
    u16 on_time __attribute__ ((packed));			/* time on (in milliseconds, 0 for forever) */
    u16 off_time __attribute__ ((packed));			/* time off (in milliseconds, 0 for forever) */
    u8 start_delay;	/* time to delay before activation (in half-seconds) */
    u8 repeat_delay;/* time to delay before repeat (in half-seconds) */
    u16 repeat:6 __attribute__ ((packed));			/* repeat count (0 for no repeat, 63 for forever) */
    u16 ex_data1:10 __attribute__ ((packed));		/* extra data specific to the accessory type */
    u8 ex_data2;	/* more extra data specific to the accessory type */
    u8 ex_data3;	/* even more extra data specific to the accessory type */
} accessory_conf_t;
enum {
    ACC_NES_MOON_GLOW,      /* Night Effects Simulator, Moon Glow light */
    ACC_NES_PHI,            /* Night Effects Simulator, Positive Hit Indicator light */
    ACC_NES_MFS,            /* Night Effects Simulator, Muzzle Flash Simulator light : ex_data1 = flash type, ex_data2 = burst count */
    ACC_SES,                /* Sound Effects Simulator : ex_data1 = track #, ex_data2 = record length (in seconds, 0 for play) */
    ACC_SMOKE,              /* Smoke generator : ex_data1 = smoke # */
    ACC_THERMAL,            /* Thermal device : ex_data1 = thermal # */
    ACC_MILES_SDH,          /* MILES, Shootback Device Holder : ex_data1 = Player ID, ex_data2 = MILES Code, ex_data3 = Ammo Type*/
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

/* this value needs to be the highest among MAX attribute values (so far, mine are all the same) */
#define NL_A_MAX GPS_A_MAX

/* commands: enumeration of all commands (functions), 
 * used by userspace application to identify command to be ececuted
 * used by kernel modules to determine command handler
 * comment format: description (type) (policy)
 */
enum {
    NL_C_UNSPEC,
    NL_C_FAILURE,	/* failure message (reply) (generic string) */
    NL_C_BATTERY,	/* battery status as percentage (request/reply) (generic 8-bit int) */
    NL_C_EXPOSE,	/* expose/conceal (command/reply) (generic 8-bit int) */
    NL_C_MOVE,		/* move as mph (command/reply) (generic 8-bit int) */
    NL_C_POSITION,	/* position in feet from home (request/reply) (generic 16-bit int) */
    NL_C_STOP,		/* stop (command/reply) (generic 8-bit int) */
    NL_C_HITS,		/* hit log (request/reply) (generic 8-bit int) */
    NL_C_HIT_CAL,	/* calibrate hit sensor (command/reply) (hit calibrate structure) */
    NL_C_BIT,		/* bit button event (broadcast) (bit event structure) */
    NL_C_ACCESSORY,	/* configure accesories (command/reply) (accessory structure) */
    NL_C_GPS,		/* gps status (request/reply) (gps structure) */
    __NL_C_MAX,
};
#define NL_C_MAX (__NL_C_MAX - 1)

#define CONCEAL 0
#define EXPOSE 1
#define LIFTING 2
#define TOGGLE 3
#define EXPOSURE_REQ 255

#define HIT_REQ 255

#endif
