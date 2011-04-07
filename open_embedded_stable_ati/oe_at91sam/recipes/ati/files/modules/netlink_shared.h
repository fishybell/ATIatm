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
    [GEN_STRING_A_MSG] = { .type = NLA_STRING },
};
/* generic policy with a single 16-bit integer attribute */
enum {
    GEN_INT16_A_UNSPEC,
    GEN_INT16_A_MSG,
    __GEN_INT16_A_MAX,
};
#define GEN_INT16_A_MAX (__GEN_INT16_A_MAX - 1)
static struct nla_policy generic_int16_policy[GEN_INT16_A_MAX + 1] = {
    [GEN_INT16_A_MSG] = { .type = NLA_U16 },
};
/* generic policy with a single 8-bit integer attribute */
enum {
    GEN_INT8_A_UNSPEC,
    GEN_INT8_A_MSG,
    __GEN_INT8_A_MAX,
};
#define GEN_INT8_A_MAX (__GEN_INT8_A_MAX - 1)
static struct nla_policy generic_int8_policy[GEN_INT8_A_MAX + 1] = {
    [GEN_INT8_A_MSG] = { .type = NLA_U8 },
};

/* specific policy for hit sensor calibration */
typedef struct hit_calibration {
    u16 lower __attribute__ ((packed)); /* lower calibration value */
    u16 upper __attribute__ ((packed)); /* upper calibration value */
    u16 burst __attribute__ ((packed)); /* burst seperation value */
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
    [HIT_A_MSG] = { .minlen = sizeof(struct hit_calibration), .maxlen = sizeof(struct hit_calibration) },
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
    [BIT_A_MSG] = { .minlen = sizeof(struct bit_event), .maxlen = sizeof(struct bit_event) },
#else
    [BIT_A_MSG] = { .len = sizeof(struct bit_event) },
#endif
};

/* this value needs to be the highest among MAX attribute values (so far, mine are all the same) */
#define NL_A_MAX BIT_A_MAX

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
    NL_C_HITS,		/* hit log (request/reply) (generic string) */
    NL_C_HIT_CAL,	/* calibrate hit sensor (command/reply) (hit calibrate structure) */
    NL_C_BIT,		/* bit button event (broadcast) (bit event structure) */
    __NL_C_MAX,
};
#define NL_C_MAX (__NL_C_MAX - 1)

#endif
