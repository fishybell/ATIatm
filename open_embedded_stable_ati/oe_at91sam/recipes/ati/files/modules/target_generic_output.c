//---------------------------------------------------------------------------
// target_generic_output.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "target.h"
#include "target_generic_output.h"
#include "netlink_kernel.h" /* for constant definitions */

//#define TESTING_ON_EVAL
#ifdef TESTING_ON_EVAL
    #undef OUTPUT_MUZZLE_FLASH
    #define OUTPUT_MUZZLE_FLASH    	AT91_PIN_PB8
    #undef OUTPUT_LED_LOW_BAT
    #define OUTPUT_LED_LOW_BAT    	AT91_PIN_PB8
#endif // TESTING_ON_EVAL

//---------------------------------------------------------------------------

#define TARGET_NAME		"generic output"


//---------------------------------------------------------------------------
// These variables are parameters given when doing an insmod (insmod blah.ko variable=5)
//---------------------------------------------------------------------------
static int has_moon = FALSE;				// has moon glow light
module_param(has_moon, bool, S_IRUGO);
static int has_muzzle = FALSE;				// has muzzle flash simulator
module_param(has_muzzle, bool, S_IRUGO);
static int has_phi = FALSE;					// has positive hit indicator light
module_param(has_phi, bool, S_IRUGO);
static int has_ses = FALSE;					// has SES
module_param(has_ses, bool, S_IRUGO);
static int has_msdh = FALSE;				// has MSDH
module_param(has_msdh, bool, S_IRUGO);
static int has_thermalX = FALSE;			// has X thermal generators
module_param(has_thermalX, int, S_IRUGO);
static int has_smokeX = FALSE;				// has X smoke generators
module_param(has_smokeX, int, S_IRUGO);
static int has_internal = FALSE;			// has internal output
module_param(has_internal, bool, S_IRUGO);

//---------------------------------------------------------------------------
// Structure for controlling generic output line
//---------------------------------------------------------------------------
typedef struct generic_output {
    int type;
    int number;
    int exists;
    GO_enable_t enabled;
    int state; // state machine state, not the get/set state for the external function
    int next_state; // state machine state, not the get/set state for the external function
    int active_on;
    GO_mode_t mode;
    int idelay;
    int rdelay;
    int on_time;
    int off_time;
    int repeat_count; // number of times to repeat
    int repeat_at; // times repeated so far
    int onoff_repeat_count; // number of times to repeat
    int onoff_repeat_at; // times repeated so far
    struct timer_list timer;
    int gpio; // at91 gpio pin
    int active; // active high/low value
    rwlock_t lock;
} generic_output_t;
//---------------------------------------------------------------------------
// States for generic output lines
//---------------------------------------------------------------------------
enum {
    S_DISABLED, // no next state, no action
    S_WAITING,  // no next state, turn line off
    S_FIRE_ON,  // fire off or waiting next, turn line on
    S_FIRE_OFF, // fire on or waiting next, turn line off
    S_STOP_ON,  // stop off or waiting next, turn line on
    S_STOP_OFF, // stop on or waiting next, turn line off
};

//---------------------------------------------------------------------------
// Declaration of state machine function
//---------------------------------------------------------------------------
static void state_run(unsigned long index);

//---------------------------------------------------------------------------
// Table of various generic output lines
//---------------------------------------------------------------------------
struct generic_output output_table[] = {
    // Internal (used for error-state flash)
    {
        ACC_INTERNAL,			// type
        1,						// number
        0,						// exists
        ENABLED,				// enabled
        S_WAITING,				// state
        S_WAITING,				// next_state
        ACTIVE_ERROR,			// active_on (activate on error/deactivate otherwise)
        BURST_FIRE,				// mode (burst fire)
        0,						// initial delay
        1000,					// repeat delay (1 second delay)
        75,						// on time (75 millisecond burst on)
        75,						// off time (75 millisecond burst off)
        10,						// repeat count (10 total fires)
        0,						// repeat at
        20,						// on/off repeat count (20 bursts per fire)
        0,						// on/off repeat at
        TIMER_INITIALIZER(state_run, 0, 0), // state function, no expire, index 0
        OUTPUT_LED_LOW_BAT,		// gpio pin
        OUTPUT_LED_LOW_BAT_ACTIVE_STATE,	// gpio active high/low
        RW_LOCK_UNLOCKED,		// lock
    },
    // MFS
    {
        ACC_NES_MFS,			// type
        1,						// number
        0,						// exists
        DISABLED,				// enabled
        S_DISABLED,				// state
        S_DISABLED,				// next_state
        0,						// active_on
        CONSTANT_ON,			// mode
        0,						// initial delay
        0,						// repeat delay
        0,						// on time
        0,						// off time
        0,						// repeat count
        0,						// repeat at
        0,						// on/off repeat count
        0,						// on/off repeat at
        TIMER_INITIALIZER(state_run, 0, 1), // state function, no expire, index 1
        OUTPUT_MUZZLE_FLASH,	// gpio pin
        OUTPUT_MUZZLE_FLASH_ACTIVE_STATE,	// gpio active high/low
        RW_LOCK_UNLOCKED,		// lock
    },
    // PHI
    {
        ACC_NES_PHI,			// type
        1,						// number
        0,						// exists
        DISABLED,				// enabled
        S_DISABLED,				// state
        S_DISABLED,				// next_state
        0,						// active_on
        CONSTANT_ON,			// mode
        0,						// initial delay
        0,						// repeat delay
        0,						// on time
        0,						// off time
        0,						// repeat count
        0,						// repeat at
        0,						// on/off repeat count
        0,						// on/off repeat at
        TIMER_INITIALIZER(state_run, 0, 2), // state function, no expire, index 2
        OUTPUT_HIT_INDICATOR,	// gpio pin
        OUTPUT_MUZZLE_FLASH_ACTIVE_STATE,	// gpio active high/low
        RW_LOCK_UNLOCKED,		// lock
    },
    // Moon Glow
    {
        ACC_NES_MGL,		// type
        1,						// number
        0,						// exists
        DISABLED,				// enabled
        S_DISABLED,				// state
        S_DISABLED,				// next_state
        0,						// active_on
        CONSTANT_ON,			// mode
        0,						// initial delay
        0,						// repeat delay
        0,						// on time
        0,						// off time
        0,						// repeat count
        0,						// repeat at
        0,						// on/off repeat count
        0,						// on/off repeat at
        TIMER_INITIALIZER(state_run, 0, 3), // state function, no expire, index 3
        OUTPUT_NIGHT_LIGHT,		// gpio pin
        OUTPUT_MUZZLE_FLASH_ACTIVE_STATE,	// gpio active high/low
        RW_LOCK_UNLOCKED,		// lock
    },
};
#define TABLE_SIZE 4 /* size of output_table */

//---------------------------------------------------------------------------
// This atomic variable is use to store the burst count.
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// Get/set functions
//---------------------------------------------------------------------------
int generic_output_exists(int type) { // returns the number that exist
    int i, have = 0; // the number of the given type I have

    // look at each item in list and count up the haves
    for (i = 0; i < TABLE_SIZE; i++) {
delay_printk("Looking for %i at %i : %i (%i)\n", type, i, output_table[i].type, output_table[i].exists);
        if (output_table[i].type == type) {
            // lock as read-only
            read_lock(&output_table[i].lock);

            // does this device exist?
            if (output_table[i].exists) {
                have++; // found one
            }

            // unlock
            read_unlock(&output_table[i].lock);
        }
    }

delay_printk("%s(): %i\n",__func__,  have);
    return have;
}
EXPORT_SYMBOL(generic_output_exists);

// get macro
#define GET_GO_VALUE(TYPE, DEF, NAME) { \
    int i; \
    TYPE found = DEF; /* found value default */ \
    /* find the correct item in the list for the given variable */ \
    for (i = 0; i < TABLE_SIZE; i++) { \
        /* check type and number */ \
        if (output_table[i].type == type && output_table[i].number == num) { /* found it */ \
            /* lock as read-only */ \
            read_lock(&output_table[i].lock); \
            /* does this device exist? */ \
            if (output_table[i].exists) { \
                found = output_table[i].NAME; \
            } \
            /* unlock */ \
            read_unlock(&output_table[i].lock); \
            break; /* stop looking */ \
        } \
    } \
/*delay_printk("%s(%i, %i): %i\n",__func__, type, num, found);*/ \
    return found; \
}

// set macro
#define SET_GO_VALUE(NAME, VALUE) { \
    int i; \
/*delay_printk("%s(%i, %i): %i\n",__func__, type, num, VALUE);*/ \
    /* find the correct item in the list for the given variable */ \
    for (i = 0; i < TABLE_SIZE; i++) { \
        /* check type and number */ \
        if (output_table[i].type == type && output_table[i].number == num) { /* found it */ \
            /* lock as read/write */ \
            write_lock(&output_table[i].lock); \
            /* does this device exist? */ \
            if (output_table[i].exists) { \
                output_table[i].NAME = VALUE; \
            } \
            /* unlock */ \
            write_unlock(&output_table[i].lock); \
            break; /* stop looking */ \
        } \
    } \
}

GO_enable_t generic_output_get_enable(int type, int num) {
    GET_GO_VALUE(GO_enable_t, DISABLED, enabled); // default to disabled
}
EXPORT_SYMBOL(generic_output_get_enable);

void generic_output_set_enable(int type, int num, GO_enable_t value) {
    // check to see if we should stop the device first
    if (value == DISABLED) {
        generic_output_set_state(type, num, OFF_IMMEDIATE); // turn off now
    }

    // set enabled value
    SET_GO_VALUE(enabled, value);
}
EXPORT_SYMBOL(generic_output_set_enable);

int generic_output_get_active_on(int type, int num) { // value ORed from above
    GET_GO_VALUE(int, 0, active_on); // default to active never
}
EXPORT_SYMBOL(generic_output_get_active_on);

void generic_output_set_active_on(int type, int num, int value) { // value ORed from above
    SET_GO_VALUE(active_on, value);
}
EXPORT_SYMBOL(generic_output_set_active_on);

GO_mode_t generic_output_get_mode(int type, int num) {
    GET_GO_VALUE(GO_mode_t, CONSTANT_ON, mode); // default to constant on
}
EXPORT_SYMBOL(generic_output_get_mode);

void generic_output_set_mode(int type, int num, GO_mode_t value) {
    SET_GO_VALUE(mode, value);
}
EXPORT_SYMBOL(generic_output_set_mode);

int generic_output_get_initial_delay(int type, int num) { // time before initial firing
    GET_GO_VALUE(int, 0, idelay); // default to 0
}
EXPORT_SYMBOL(generic_output_get_initial_delay);

void generic_output_set_initial_delay(int type, int num, int msecs) {
    SET_GO_VALUE(idelay, msecs);
}
EXPORT_SYMBOL(generic_output_set_initial_delay);

int generic_output_get_repeat_delay(int type, int num) { // time between repeats
    GET_GO_VALUE(int, 0, rdelay); // default to 0
}
EXPORT_SYMBOL(generic_output_get_repeat_delay);

void generic_output_set_repeat_delay(int type, int num, int msecs) {
    SET_GO_VALUE(rdelay, msecs);
}
EXPORT_SYMBOL(generic_output_set_repeat_delay);

int generic_output_get_repeat_count(int type, int num) { // repeat X times (-1 for infinity, 0 for never)
    GET_GO_VALUE(int, 0, repeat_count); // default to 0
}
EXPORT_SYMBOL(generic_output_get_repeat_count);

void generic_output_set_repeat_count(int type, int num, int count) {
    SET_GO_VALUE(repeat_count, count);
}
EXPORT_SYMBOL(generic_output_set_repeat_count);

int generic_output_get_on_time(int type, int num) { // active time
    GET_GO_VALUE(int, 0, on_time); // default to 0
}
EXPORT_SYMBOL(generic_output_get_on_time);

void generic_output_set_on_time(int type, int num, int msecs) {
    SET_GO_VALUE(on_time, msecs);
}
EXPORT_SYMBOL(generic_output_set_on_time);

int generic_output_get_off_time(int type, int num) { // inactive time
    GET_GO_VALUE(int, 0, off_time); // default to 0
}
EXPORT_SYMBOL(generic_output_get_off_time);

void generic_output_set_off_time(int type, int num, int msecs) {
    SET_GO_VALUE(off_time, msecs);
}
EXPORT_SYMBOL(generic_output_set_off_time);

int generic_output_get_onoff_repeat(int type, int num) { // repeat on/off cycle X times (-1 for infinity, 0 for never)
    GET_GO_VALUE(int, 0, onoff_repeat_count); // default to 0
}
EXPORT_SYMBOL(generic_output_get_onoff_repeat);

void generic_output_set_onoff_repeat(int type, int num, int count) {
    SET_GO_VALUE(onoff_repeat_count, count);
}
EXPORT_SYMBOL(generic_output_set_onoff_repeat);

//---------------------------------------------------------------------------
// Functions to manipulate the state machine for an output line
//---------------------------------------------------------------------------
GO_state_t generic_output_get_state(int type, int num) {
    int i;
    GO_state_t found = OFF_IMMEDIATE; // found default is off

    // find the correct item in the list and compute the GO_state_t from the current state
    for (i = 0; i < TABLE_SIZE; i++) {
        // check type and number
        if (output_table[i].type == type && output_table[i].number == num) { // found it
            // lock as read-only
            read_lock(&output_table[i].lock);

            // does this device exist and is it enabled?
            if (output_table[i].exists && output_table[i].enabled == ENABLED) {
                switch (output_table[i].state) { // look at current state not next_state
                    default:
                    case S_DISABLED:
                        // use default
                        break;
                    case S_FIRE_OFF:
                    case S_WAITING:
                        found = OFF_SOON;
                        break;
                    case S_FIRE_ON:
                        found = ON_SOON;
                        break;
                }
            }

            // unlock
            read_unlock(&output_table[i].lock);

            break; // stop looking
        }
    }
delay_printk("%s(%i, %i): %i\n",__func__, type, num, found);
    return found;
}
EXPORT_SYMBOL(generic_output_get_state);

// wrapper around the mod_timer function for code clarity
inline void schedule_timer(int index, int msecs) {
    mod_timer(&output_table[index].timer, jiffies+((msecs*HZ)/1000));
}

// helper function to change the internal state using GO_state_t values for a given index (assumes already locked)
void index_set_state(int i, GO_state_t value) {
//delay_printk("%s(%i): %i\n",__func__, i, value);
    // does this device exist and is it enabled?
    if (output_table[i].exists && output_table[i].enabled == ENABLED) {
        switch (value) {
           case ON_IMMEDIATE:	// on now, only ignores initial delay value
               output_table[i].next_state = S_FIRE_ON; // turn on
               schedule_timer(i, 0); // immediately
               break;
           case OFF_IMMEDIATE:	// off immediate (stops current burst, repeat, etc.)
               output_table[i].next_state = S_WAITING; // turn off
               schedule_timer(i, 0); // immediately
               break;
           case   ON_SOON:		// manual on (uses initial delay)
               output_table[i].next_state = S_FIRE_ON; // turn on
               schedule_timer(i, output_table[i].idelay); // after initial delay
               break;
           case   OFF_SOON:		// manual off (waits for current burst, repeat, etc., cancels infinite repeat)
               if (output_table[i].mode == CONSTANT_ON) { // turn off immediately if we're constant on
                   output_table[i].next_state = S_WAITING; // turn off
                   schedule_timer(i, 0);
               } else if (output_table[i].next_state == S_FIRE_ON) { // 
                   output_table[i].next_state = S_STOP_ON; // turn on next, off soon
                   // timer running already, let it run its course
               } else if (output_table[i].next_state == S_FIRE_OFF) {
                   output_table[i].next_state = S_STOP_OFF; // turn off next, off soon
                   // timer running already, let it run its course
               } else {
                   output_table[i].next_state = S_WAITING; // turn off
                   schedule_timer(i, 0); // immediately
               }
               break;
        }
    }
}

void generic_output_set_state(int type, int num, GO_state_t value) {
    int i;
delay_printk("%s(%i, %i): %i\n",__func__, type, num, value);

    // find the correct item in the list and compute the next state from the given GO_state_t
    for (i = 0; i < TABLE_SIZE; i++) {
        // check type and number
        if (output_table[i].type == type && output_table[i].number == num) { // found it
            // lock as read/write
            write_lock(&output_table[i].lock);

            index_set_state(i, value); // use helper function to change internal state

            // unlock
            write_unlock(&output_table[i].lock);

            break; // stop looking
        }
    }
}
EXPORT_SYMBOL(generic_output_set_state);

//---------------------------------------------------------------------------
// Event handler routines and event queue data
//---------------------------------------------------------------------------
typedef struct event_item {
    GO_event_t type;
    struct event_item *next;
} event_item_t;
struct event_item *start = NULL; // start of queue
struct event_item *end = NULL; // end of queue
static spinlock_t e_lock = SPIN_LOCK_UNLOCKED; // queue lock
static struct work_struct event_work; // event work queue

static void handle_event(struct work_struct * work) { // actual handler: can't be called by interrupt, so use a queue
    int i;
    struct event_item top; // not a pointer
    int type;
    
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }

    // lock
    spin_lock(&e_lock);

    // do we have a full queue?
    if (start == NULL) {
        // no, so don't do anything
        spin_unlock(&e_lock);
        return;
    }

    // make a copy of the top queue item
    top = *start;

    // free allocated memory
    kfree(start);

    // rearrange the queue
    start = top.next; // new start
    if (top.next == NULL) {
        end = NULL; // no ending
    }
    
    // do we still have items on the queue?
    if (start != NULL) {
        // schedule more work
        schedule_work(&event_work);
    }

    // unlock
    spin_unlock(&e_lock);

    // start handling of event
    type = top.type;

delay_printk("%s(): %i\n",__func__, type);
    // change the state of all effected items in the table
    for (i = 0; i < TABLE_SIZE; i++) {
        // lock as read/write
        write_lock(&output_table[i].lock);

        // does this device exist and is it enabled?
        if (output_table[i].exists && output_table[i].enabled == ENABLED) {
            GO_state_t state = ON_IMMEDIATE; // use on_immediate to tell whether I changed the value or not

            // change the state depending on the event type and what the output is activated on
            switch (type) {
                case EVENT_RAISE:	// start of raise
                    if (output_table[i].active_on & ACTIVE_RAISE) {
                        state = ON_SOON;
                    }
                    if (output_table[i].active_on & UNACTIVE_RAISE) { // don't do as else: overwrite to off if both are set
                        state = OFF_SOON; // started, so soon
                    }
                    break;
                case EVENT_UP:		// finished raising
                    if (output_table[i].active_on & ACTIVE_UP) {
                        state = ON_SOON;
                    }
                    if (output_table[i].active_on & UNACTIVE_UP) { // don't do as else: overwrite to off if both are set
                        state = OFF_IMMEDIATE; // finished, so immediate
                    }
                    break;
                case EVENT_LOWER:	// start of lower
                    if (output_table[i].active_on & ACTIVE_LOWER) {
                        state = ON_SOON;
                    }
                    if (output_table[i].active_on & UNACTIVE_LOWER) { // don't do as else: overwrite to off if both are set
                        state = OFF_SOON; // started, so soon
                    }
                    break;
                case EVENT_DOWN:	// finished lowering
                    if (output_table[i].active_on & ACTIVE_DOWN) {
                        state = ON_SOON;
                    }
                    if (output_table[i].active_on & UNACTIVE_DOWN) { // don't do as else: overwrite to off if both are set
                        state = OFF_IMMEDIATE; // finished, so immediate
                    }
                    break;
                case EVENT_MOVE:	// start of move
                    if (output_table[i].active_on & ACTIVE_MOVE) {
                        state = ON_SOON;
                    }
                    if (output_table[i].active_on & UNACTIVE_MOVE) { // don't do as else: overwrite to off if both are set
                        state = OFF_SOON; // started, so soon
                    }
                    break;
                case EVENT_MOVING:	// reached target speed
                    if (output_table[i].active_on & ACTIVE_MOVING) {
                        state = ON_SOON;
                    }
                    if (output_table[i].active_on & UNACTIVE_MOVING) { // don't do as else: overwrite to off if both are set
                        state = OFF_IMMEDIATE; // finished, so immediate
                    }
                    break;
                case EVENT_COAST:	// started coast
                    if (output_table[i].active_on & ACTIVE_COAST) {
                        state = ON_SOON;
                    }
                    if (output_table[i].active_on & UNACTIVE_COAST) { // don't do as else: overwrite to off if both are set
                        state = OFF_SOON; // started, so soon
                    }
                    break;
                case EVENT_STOP:	// started stopping
                    if (output_table[i].active_on & ACTIVE_STOP) {
                        state = ON_SOON;
                    }
                    if (output_table[i].active_on & UNACTIVE_STOP) { // don't do as else: overwrite to off if both are set
                        state = OFF_SOON; // started, so soon
                    }
                    break;
                case EVENT_STOPPED:	// finished stopping
                    if (output_table[i].active_on & ACTIVE_STOPPED) {
                        state = ON_SOON;
                    }
                    if (output_table[i].active_on & UNACTIVE_STOPPED) { // don't do as else: overwrite to off if both are set
                        state = OFF_IMMEDIATE; // finished, so immediate
                    }
                    break;
                case EVENT_HIT:		// hit
                    if (output_table[i].active_on & ACTIVE_HIT) {
                        state = ON_SOON;
                    }
                    if (output_table[i].active_on & UNACTIVE_HIT) { // don't do as else: overwrite to off if both are set
                        state = OFF_IMMEDIATE; // immediate action is better
                    }
                    break;
                case EVENT_KILL:	// kill
                    if (output_table[i].active_on & ACTIVE_KILL) {
                        state = ON_SOON;
                    }
                    if (output_table[i].active_on & UNACTIVE_KILL) { // don't do as else: overwrite to off if both are set
                        state = OFF_IMMEDIATE; // immediate action is better
                    }
                    break;
                case EVENT_ERROR:	// error with one of the above
                    if (output_table[i].active_on & ACTIVE_ERROR) {
                        state = ON_SOON;
                    } else {
                        state = OFF_IMMEDIATE; // immediate off for error regardless of what flags are set
                    }
                    break;
            }

            // special case for devices marked ACTIVE_ERROR
            if (type != EVENT_ERROR && output_table[i].active_on & ACTIVE_ERROR) {
                state = OFF_IMMEDIATE; // disable error output on normal operation
            }

            // do we require a change?
            if (state != ON_IMMEDIATE) {
                index_set_state(i, state); // use helper function to change internal state
            }

            // if we had an error, disable until re-enabled (doesn't lose configuration info)
            if (type == EVENT_ERROR && !(output_table[i].active_on & ACTIVE_ERROR)) {
                // we disable after index_set_state() call so it will function as normal
                output_table[i].enabled = DISABLED; // will prevent another event from activating the accessory
            }
        }

        // unlock
        write_unlock(&output_table[i].lock);
    }
}

void generic_output_event(GO_event_t type) { // can be called from interrupt (by use of a queue)
    struct event_item *msg = NULL;

    // allocate message buffer
    msg = kmalloc(sizeof(struct event_item), GFP_KERNEL); // don't use GFP_ATOMIC as we would like to always work if there is memory available
    if (msg == NULL) {
        // out of memory
        return;
    }

    // fill in data
    msg->type = type;

    // Put at the end of the message queue

    // lock
    spin_lock(&e_lock);

    // add to end of queue
    msg->next = NULL;
    if (end == NULL) {
        // also the start
        start = msg;
    } else {
        // move end back
        end->next = msg;
    }
    end = msg; // new is always the end

    // unlock
    spin_unlock(&e_lock);

    // schedule work
    schedule_work(&event_work);
delay_printk("Scheduled event %i\n", type);
}
EXPORT_SYMBOL(generic_output_event);

//---------------------------------------------------------------------------
// Move repeat variables forward and return if we repeat this time or not
//---------------------------------------------------------------------------
int do_repeat(int count, int *at) {
    // move repeat counter
    (*at)++;

    // if we're infinite repeat, reset counter
    if (count == -1) {
        *at = 0;
        // return 1 for more repeats
        return 1;
    }

    // check if repeat cycle finished
    if (*at > count) {
        // reset counter
        *at = 0;

        // return 0 for no more repeats
        return 0;
    } else {
        // return 1 for more repeats
        return 1;
    }
}



//---------------------------------------------------------------------------
// Run a cycle on the state machine for a given index
// NOTE: Only call from a timer
//---------------------------------------------------------------------------
static void state_run(unsigned long index) {
    struct generic_output *this = &output_table[index];
    int add_delay = 0; // additional delay to add to timer

// delay_printk("%s(): %i\n",__func__, index);
    // lock as read/write
    write_lock(&this->lock);
// delay_printk("i %i\ns %i\nn %i\nm %i\n", index, this->state, this->next_state, this->mode);

    // change current state to next state
    this->state = this->next_state;

    // ignore disabled/enabled, just do the current state actions
    switch (this->state) {
        case S_DISABLED: // no next state, no action
            // reset counter variables
            this->repeat_at = 0;
            this->onoff_repeat_at = 0;

            // no next state (or leave same)

            // no action on output line

            // no timer change
            break;

        case S_WAITING:  // no next state, turn line off
            // reset counter variables
            this->repeat_at = 0;
            this->onoff_repeat_at = 0;

            // no next state (or leave same)

            // output line off
            at91_set_gpio_output(this->gpio, !this->active);

            // no timer change
            break;

        case S_FIRE_ON:  // fire off or waiting next, turn line on
        case S_STOP_ON:  // stop off or waiting next, turn line on
            // output line on
            at91_set_gpio_output(this->gpio, this->active);

            // timer and next state depend on mode
            switch (this->mode) {
                case CONSTANT_ON: // goes on, stays on (no repeating)
                    // change next state
                    if (this->state == S_STOP_ON) {
                        this->next_state = S_WAITING; // is on, next off
                    } else {
                        this->next_state = S_FIRE_ON; // is on, stays on
                    }

                    // no timer change (stays on)
                    break;
                case TEMP_ON:
                case BURST_FIRE:
                    // change next state
                    if (this->state == S_FIRE_ON) {
                        this->next_state = S_FIRE_OFF; // is on, next off, potential on again
                    } else {
                        this->next_state = S_STOP_OFF; // is on, next off, potential on again
                    }

                    // change timer
                    schedule_timer(index, this->on_time); // next state after "on time"
                    break;
            }
            break;

        case S_FIRE_OFF: // fire on or waiting next, turn line off
        case S_STOP_OFF: // stop on or waiting next, turn line off
            // output line off
            at91_set_gpio_output(this->gpio, !this->active);

            // next state depends on mode
            switch (this->mode) {
                case CONSTANT_ON: // goes on, stays on (no repeating)
                    // change next state
                    this->next_state = S_WAITING; // is off, next off (shouldn't get here)
                    break;
                case BURST_FIRE:
                    // move on/off repeat variables and repeat if necessary
                    if (do_repeat(this->onoff_repeat_count, &this->onoff_repeat_at)) {
                        // do on/off repeat: next state is off, then on again
                        if (this->state == S_FIRE_OFF) {
// delay_printk("burst repeating %i\n", this->onoff_repeat_at);
                            this->next_state = S_FIRE_ON; // is off, next on
                        } else {
                            // on stop condition, let it follow through but cancel infinite repeat
                            if (this->onoff_repeat_count == -1) {
// delay_printk("burst infinite repeat stopped\n");
                                this->next_state = S_WAITING; // is off, next off (stay off)
                            } else {
// delay_printk("burst repeating %i\n", this->onoff_repeat_at);
                                this->next_state = S_STOP_ON; // is off, next on
                            }
                        }
                    } else {
                        // don't on/off repeat:
                        // move normal repeat variables and repeat if necessary
                        if (do_repeat(this->repeat_count, &this->repeat_at)) {
                            // do normal repeat: next state is on, then off again
                            if (this->state == S_FIRE_OFF) {
// delay_printk("repeating %i\n", this->repeat_at);
                                this->next_state = S_FIRE_ON; // is off, next on
                                add_delay = this->rdelay; // add repeat delay to delay time
                            } else {
                                // on stop condition, let it follow through but cancel infinite repeat
                                if (this->repeat_count == -1) {
// delay_printk("infinite repeat stopped\n");
                                    this->next_state = S_WAITING; // is off, next off (stay off)
                                } else {
// delay_printk("repeating %i\n", this->repeat_at);
                                    this->next_state = S_STOP_ON; // is off, next on
                                    add_delay = this->rdelay; // add repeat delay to delay time

                                }
                            }
                        } else {
                            // don't normal repeat: next state is off
                            this->next_state = S_WAITING; // is off, next off (stay off)
                        }
                    }
                    break;

                case TEMP_ON:
                    // move repeat variables and repeat if necessary
                    if (do_repeat(this->repeat_count, &this->repeat_at)) {
                        // do repeat: next state is on, then off again
                        if (this->state == S_FIRE_OFF) {
// delay_printk("repeating %i\n", this->repeat_at);
                            this->next_state = S_FIRE_ON; // is off, next on
                            add_delay = this->rdelay; // add repeat delay to delay time
                        } else {
                            // on stop condition, let it follow through but cancel infinite repeat
                            if (this->repeat_count == -1) {
// delay_printk("infinite repeat stopped\n");
                                this->next_state = S_WAITING; // is off, next off (stay off)
                            } else {
// delay_printk("repeating %i\n", this->repeat_at);
                                this->next_state = S_STOP_ON; // is off, next on
                                add_delay = this->rdelay; // add repeat delay to delay time
                            }
                        }
                    } else {
                        // don't repeat: next state is off
                        this->next_state = S_WAITING; // is off, next off
                    }
                    break;
            }

            // timer change depends on next state
            switch (this->next_state) {
                default:
                case S_DISABLED:
                case S_WAITING:
                    schedule_timer(index, 0); // next state immediately
                case S_FIRE_ON:
                case S_FIRE_OFF:
                case S_STOP_ON:
                case S_STOP_OFF:
                    if (this->off_time+add_delay > 0) {
                        schedule_timer(index, (this->off_time+add_delay)); // next state after "off time" and additional delay together
                    }
                    break;
            }
            break;
    }

    // unlock
    write_unlock(&this->lock);

//delay_printk("%s() %i to %i\n",__func__,  this->state, this->next_state);
}


//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void) {
    int status = 0, i;

    // configure flash gpio for output and set initial output
    for (i = 0; i < TABLE_SIZE; i++) {
delay_printk("Looking at %i : %i\n", i, output_table[i].type);
        // lock for read/write
        write_lock(&output_table[i].lock);

        // determine if this item should exist
        switch (output_table[i].type) {
            case ACC_NES_MGL:
                // only one moon glow light
                if (has_moon && output_table[i].number == 1) {
delay_printk("Has Moon\n");
                    output_table[i].exists = 1; // it exists
                }
                break;
            case ACC_NES_PHI:
                // only one positive hit indicator
                if (has_phi && output_table[i].number == 1) {
delay_printk("Has PHI\n");
                    output_table[i].exists = 1; // it exists
                }
                break;
            case ACC_NES_MFS:
                // only one muzzle flash simulator
                if (has_muzzle && output_table[i].number == 1) {
delay_printk("Has MFS\n");
                    output_table[i].exists = 1; // it exists
                }
                break;
            case ACC_SES:
                // only one ses
                if (has_ses && output_table[i].number == 1) {
delay_printk("Has SES\n");
                    output_table[i].exists = 1; // it exists
                }
                break;
            case ACC_MILES_SDH:
                // only one moon glow light
                if (has_msdh && output_table[i].number == 1) {
delay_printk("Has MSDH\n");
                    output_table[i].exists = 1; // it exists
                }
                break;
            case ACC_THERMAL:
                // potentially multiple thermal generators
                if (output_table[i].number <= has_thermalX) {
delay_printk("Has Thermal %i\n", output_table[i].number);
                    output_table[i].exists = 1; // it exists
                }
                break;
            case ACC_SMOKE:
                // potentially multiple smoke generators
                if (output_table[i].number <= has_smokeX) {
delay_printk("Has Smoke %i\n", output_table[i].number);
                    output_table[i].exists = 1; // it exists
                }
                break;
            case ACC_INTERNAL:
                // only one muzzle flash simulator
                if (has_internal && output_table[i].number == 1) {
delay_printk("Has Internal\n");
                    output_table[i].exists = 1; // it exists
                }
                break;
        }
        // if it exists, initialize the gpio line
        if (output_table[i].exists) {
            // initialize as inactive
            at91_set_gpio_output(output_table[i].gpio, !output_table[i].active);
        }

        // unlock
        read_unlock(&output_table[i].lock);
    }

    return status;
}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_exit(void) {
    int i;

    // disable access from external functions
    atomic_set(&full_init, FALSE);

    // stop everything and turn everything off
    for (i = 0; i < TABLE_SIZE; i++) {
        // lock for read/write
        write_lock(&output_table[i].lock);

        // delete timer no matter what
        del_timer(&output_table[i].timer);

        // only disable what we enabled
        if (output_table[i].exists) {
            // stop state machine
            output_table[i].state = S_DISABLED;

            // turn off output
            at91_set_gpio_output(output_table[i].gpio, !output_table[i].active);
        }

        // unlock
        read_unlock(&output_table[i].lock);
    }

    return 0;
}

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_generic_output_init(void) {
delay_printk("%s(): %s - %s\n",__func__,  __DATE__, __TIME__);
    hardware_init();

	INIT_WORK(&event_work, handle_event);

    // signal that we are fully initialized
    atomic_set(&full_init, TRUE);
    return 0;
}

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_generic_output_exit(void) {
    atomic_set(&full_init, FALSE);
    ati_flush_work(&event_work); // close any open work queue items
    hardware_exit();
}


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_generic_output_init);
module_exit(target_generic_output_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target muzzle flash simulator module");
MODULE_AUTHOR("jpy");

