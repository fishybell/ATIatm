#ifndef NETLINK_KERNEL_H
#define NETLINK_KERNEL_H

#include <net/genetlink.h>
#include <mach/heartbeat.h>

/* includes the command id numbers and other shared user/kernel data */
#include "netlink_shared.h"

/* command handler callback
   returns: explained in enum below
   arg1: message data in form userspace
   arg2: message data out to userspace
   arg3: command id number
   arg4: ident field from the nl_driver table */
typedef int (*nl_command_callback)(struct genl_info*, struct sk_buff*, int, void*);
enum {
    HANDLE_SUCCESS, /* success, send given reply message */
    HANDLE_FAILURE, /* failure, send default failure message */
    HANDLE_FAILURE_MESSAGE, /* failure, send given reply message */
    HANDLE_SUCCESS_NO_REPLY, /* success, don't send any message */
    HANDLE_FAILURE_NO_REPLY, /* failure, don't send any message */
};

/* command id to command handler mapping structure */
typedef struct driver_command {
    int command; // command id to handle (multiple handlers will all be called)
    nl_command_callback handler; // command handler callback
} driver_command_t;

/* structure to hold all command handlers for an entire driver */
typedef struct nl_driver {
    struct heartbeat_object *hb_obj; // optional heartbeat object
    struct driver_command *commands; // array of commands to register
    int num_commands; // size of command array
    void *ident; // optional identifying data passed to commands
} nl_driver_t;

/* installer function to start handling userspace->kernel messages
   returns: negative on failure, 0 or positive on success */
extern int install_nl_driver(const struct nl_driver*);

/* uninstaller function to stop handling userspace->kernel messages
   arg1: the return value of the installer function */
extern void uninstall_nl_driver(int);

/* callback to fill an unrequested kernel->userspace message with data
   returns: 0 on success, 1 on failure
   arg1: message buffer
   arg2: arbitrary message data from below */
typedef int (*message_filler_handler)(struct sk_buff*, void*);
/* when a driver wants to send unrequested data from kernel->userspace
   it needs to call one of these functions. *_uni for targeted messages,
   and *_multi for global messages. No reply is to be expected, so any
   replies will get sent to a registered command.
   returns: 0 on success or no recipients, 1 on failure
   arg1: arbitrary message data
   arg2: message data callback (called when the buffer is ready for it)
   arg3: command id number
   arg4: destination pid (unicast only) */
extern int send_nl_message_uni(void*, message_filler_handler, int, int);
extern int send_nl_message_multi(void*, message_filler_handler, int);

/* when a driver needs to send multiple multicast messages in a
   short period of time (ie. NL_C_EVENT messages) this interface
   provides a queue similar in speed to calling delay_printk().
   normally a module should use work queues and timers and call
   send_nl_message_multi directly as it takes less memory and
   allows for failure handling.
   returns: nothing (not guaranteed to pass or fail)
   arg1: command id number
   arg2: arbitrary message data (will be put in as 1st attribute)
   arg3: size of data */
extern void queue_nl_multi(int,void*,size_t);

#endif
