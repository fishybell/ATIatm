#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>

#include "delay_printk.h"

#define FALSE				0
#define TRUE				1

// small printk message buffer, so use wisely
#define MAX_PRINTK_MSG 124 /* 128 - sizeof(void*) */

// printk buffer queue
typedef struct printk_buffer {
    char message[MAX_PRINTK_MSG];
    struct printk_buffer *next;
} printk_buffer_t;
struct printk_buffer *start = NULL; // start of queue
struct printk_buffer *end = NULL; // end of queue

//---------------------------------------------------------------------------
// Global queue lock
//---------------------------------------------------------------------------
static spinlock_t q_lock = SPIN_LOCK_UNLOCKED;

//---------------------------------------------------------------------------
// This work queue item is used to delay the operation of a printk call
//---------------------------------------------------------------------------
static struct work_struct printk_work;

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// Work item to do the actual printk call
//---------------------------------------------------------------------------
static void printk_do_work(struct work_struct * work) {
    struct printk_buffer top; // not a pointer
    int s;

    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
    // lock
    spin_lock(&q_lock);

    // do we have a full queue?
    if (start == NULL) {
        // no, so don't do anything
        spin_unlock(&q_lock);
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
        schedule_work(&printk_work);
    }

    // unlock
    spin_unlock(&q_lock);

    // do the actual print outside the lock
    for (s=0; s<MAX_PRINTK_MSG && top.message[s]; s++) {
        printk("%c", top.message[s]);
    }
}

//---------------------------------------------------------------------------
// printk style function for delayed printing
//---------------------------------------------------------------------------
signed int delay_printk(const char *pFormat, ...)
{
    va_list ap;
    signed int result=1;
    struct printk_buffer *msg = NULL;

    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return result;
    }
     // check valid format first
    if (pFormat == NULL) {
       return result;
    }

    // allocate message buffer
    // use GFP_ATOMIC as it's quicker than GFP_KERNEL
    msg = kmalloc(sizeof(struct printk_buffer), GFP_ATOMIC);
    if (msg == NULL) {
        // out of memory
        return -ENOMEM;
    }

    // Forward call to vsnprintf to do the actual formatting
    va_start(ap, pFormat);
    result = vsnprintf(msg->message, MAX_PRINTK_MSG, pFormat, ap);
    va_end(ap);

    // Put at the end of the message queue
    if (result > 0) {
        // lock
        spin_lock(&q_lock);

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
        spin_unlock(&q_lock);

        // schedule work
        schedule_work(&printk_work);
    }

    return result;
}
EXPORT_SYMBOL(delay_printk);

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
void ati_flush_work(struct work_struct *work) {
    printk("flushing...");
    if (work != NULL) {
       flush_work(work);
    }
    printk("flushed\n");
}

EXPORT_SYMBOL(ati_flush_work);

static int __init delay_printk_init(void) {
    INIT_WORK(&printk_work, printk_do_work);
    printk("INIT DELAY PRINTK MODULE\n");

    atomic_set(&full_init, TRUE);
    return 0;
}

static void __exit delay_printk_exit(void) {
    atomic_set(&full_init, FALSE);
    flush_work(&printk_work); // close any open work queue items
    printk("EXIT DELAY PRINTK MODULE\n");
}

module_init(delay_printk_init);
module_exit(delay_printk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ATI delayed printk module");
MODULE_AUTHOR("ndb");

