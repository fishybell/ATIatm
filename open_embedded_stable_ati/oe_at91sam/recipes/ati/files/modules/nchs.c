//---------------------------------------------------------------------------
// target_mover_generic.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>
#include <linux/atmel_tc.h>
#include <linux/clk.h>
#include <linux/atmel_pwm.h>

#include "netlink_kernel.h"
#include "target.h"
#include "nchs.h"


//---------------------------------------------------------------------------
#define NCHS_BLOCK 0
#define CHANNEL_A 0
#define CHANNEL_B 1
#define PIN_L 0
#define PIN_U 1

//---------------------------------------------------------------------------
#define ENCODER_DELAY_IN_MSECONDS 1000
#define MAX_TIME	0x10000

// structure to access the timer counter registers
static struct atmel_tc * tc = NULL;

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the encoder timers fire
//---------------------------------------------------------------------------
static void encoder_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the delayed update for encoder
//---------------------------------------------------------------------------
static struct timer_list encoder_timer_list = TIMER_INITIALIZER(encoder_fire, 0, 0);

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// Atomic variables for remembering rod A/B upper/lower time values
//---------------------------------------------------------------------------
atomic_t count_al = ATOMIC_INIT(0); // Rod A, Lower
atomic_t count_au = ATOMIC_INIT(0); // Rod B, Upper
atomic_t count_bl = ATOMIC_INIT(0); // Rod A, Lower
atomic_t count_bu = ATOMIC_INIT(0); // Rod B, Upper
atomic_t lock_al = ATOMIC_INIT(-1); // unlocked A, Lower
atomic_t lock_au = ATOMIC_INIT(-1); // unlocked A, Upper
atomic_t lock_bl = ATOMIC_INIT(-1); // unlocked B, Lower
atomic_t lock_bu = ATOMIC_INIT(-1); // unlocked B, Upper
atomic_t over_al = ATOMIC_INIT(0); // overflow A, Lower
atomic_t over_au = ATOMIC_INIT(0); // overflow A, Upper
atomic_t over_bl = ATOMIC_INIT(0); // overflow B, Lower
atomic_t over_bu = ATOMIC_INIT(0); // overflow B, Upper

//---------------------------------------------------------------------------
// an encoder pin (A/B, upper/lower) tripped
//---------------------------------------------------------------------------
void encoder_pin(int channel, int pin, u32 count) {
   atomic_t *a_count, *a_lock; // atomic counter/lock
   // check if we can interrupt yet
   if (!atomic_read(&full_init)) { return; }
   if (!tc) { return; }

   // user appropriate counters/locks
   if (channel == CHANNEL_A) {
      if (pin == PIN_L) {
         a_count = &count_al;
         a_lock = &lock_al;
      } else if (pin == PIN_U) {
         a_count = &count_au;
         a_lock = &lock_au;
      } else {
         return;
      }
   } else if (channel == CHANNEL_B) {
      if (pin == PIN_L) {
         a_count = &count_bl;
         a_lock = &lock_bl;
      } else if (pin == PIN_U) {
         a_count = &count_bu;
         a_lock = &lock_bu;
      } else {
         return;
      }
   } else {
      return;
   }

   // run through the needed steps for when one of the four pins trips
   if (atomic_inc_return(a_lock) == 0) { // test if the lock was open
      // TODO -- calculate from last time and overflow
      atomic_set(a_count, count);
      mod_timer(&encoder_timer_list, jiffies+((ENCODER_DELAY_IN_MSECONDS*HZ)/1000)); // show values later
   } else {
      atomic_dec(a_lock); // lock was closed
   }
}

//---------------------------------------------------------------------------
// timer counter overflow interrupt
//---------------------------------------------------------------------------
irqreturn_t encoder_tc_int(int irq, void *dev_id, struct pt_regs *regs) { // channel a, lower pin
   u32 status;
   // read registers
   status = __raw_readl(tc->regs + ATMEL_TC_REG(CHANNEL_A, SR)); // status register

   // check if we can interrupt yet
   if (!atomic_read(&full_init)) {
      return IRQ_HANDLED;
   }
   if (!tc) return IRQ_HANDLED;

   // Overlflow caused IRQ?
   if ( status & ATMEL_TC_COVFS ) {
      // overflow work (fix each item as quickly as possible)

      // rod A, lower
      if (atomic_read(&count_al) == -1) { // has value?
         atomic_inc(&over_al); // set overflow
      }

      // rod A, upper
      if (atomic_read(&count_au) == -1) { // has value?
         atomic_inc(&over_au); // set overflow
      }

      // rod B, lower
      if (atomic_read(&count_bl) == -1) { // has value?
         atomic_inc(&over_bl); // set overflow
      }

      // rod B, upper
      if (atomic_read(&count_bu) == -1) { // has value?
         atomic_inc(&over_bu); // set overflow
      }

   }

   return IRQ_HANDLED;
}

//---------------------------------------------------------------------------
// an encoder pin tripped
//---------------------------------------------------------------------------
irqreturn_t encoder_a_l_int(int irq, void *dev_id, struct pt_regs *regs) { // channel a, lower pin
   u32 count = __raw_readl(tc->regs + ATMEL_TC_REG(CHANNEL_A, CV));
   encoder_pin(CHANNEL_A,PIN_L,count);
   return IRQ_HANDLED;
}
irqreturn_t encoder_a_u_int(int irq, void *dev_id, struct pt_regs *regs) { // channel a, upper pin
   u32 count = __raw_readl(tc->regs + ATMEL_TC_REG(CHANNEL_A, CV));
   encoder_pin(CHANNEL_A,PIN_U,count);
   return IRQ_HANDLED;
}
irqreturn_t encoder_b_l_int(int irq, void *dev_id, struct pt_regs *regs) { // channel b, lower pin
   u32 count = __raw_readl(tc->regs + ATMEL_TC_REG(CHANNEL_A, CV));
   encoder_pin(CHANNEL_B,PIN_L,count);
   return IRQ_HANDLED;
}
irqreturn_t encoder_b_u_int(int irq, void *dev_id, struct pt_regs *regs) { // channel b, upper pin
   u32 count = __raw_readl(tc->regs + ATMEL_TC_REG(CHANNEL_A, CV));
   encoder_pin(CHANNEL_B,PIN_U,count);
   return IRQ_HANDLED;
}

//---------------------------------------------------------------------------
// The function that gets called when the encoder timer fires.
//---------------------------------------------------------------------------
static void encoder_fire(unsigned long data) {
   int al, au, bl, bu; // counters
   int mc = 0x7fffffff; // minimum counter
   if (!atomic_read(&full_init)) {
      return;
   }

   // lock
   atomic_inc(&lock_al);
   atomic_inc(&lock_au);
   atomic_inc(&lock_bl);
   atomic_inc(&lock_bu);

   // read values with overflow
   al = atomic_read(&count_al) + (MAX_TIME * atomic_read(&over_al));
   au = atomic_read(&count_au) + (MAX_TIME * atomic_read(&over_au));
   bl = atomic_read(&count_bl) + (MAX_TIME * atomic_read(&over_bl));
   bu = atomic_read(&count_bu) + (MAX_TIME * atomic_read(&over_bu));

   // reset 
   atomic_set(&count_al,-1);
   atomic_set(&count_au,-1);
   atomic_set(&count_bl,-1);
   atomic_set(&count_bu,-1);

   // reset overflow
   atomic_set(&over_al, 0);
   atomic_set(&over_au, 0);
   atomic_set(&over_bl, 0);
   atomic_set(&over_bu, 0);

   // unlock
   atomic_set(&lock_al, -1);
   atomic_set(&lock_au, -1);
   atomic_set(&lock_bl, -1);
   atomic_set(&lock_bu, -1);

   // find minimum
   mc = min(al, mc);
   mc = min(au, mc);
   mc = min(bl, mc);
   mc = min(bu, mc);

   // check valid values
   if (mc >= 0) {
      // re-calculate new values based on minimum
      al -= mc;
      au -= mc;
      bl -= mc;
      bu -= mc;

      // print values
      delay_printk("\nAl: %i\tAu: %i\tBl: %i\tBu: %i\n", al, au, bl, bu);
      delay_printk("dA: %i\tdB: %i\n", au - al, bu - bl);
   } else {
      delay_printk("\nerror on receipt: %i %i %i %i\n", al, au, bl, bu);
   }
}

//---------------------------------------------------------------------------
// set an IRQ for a GPIO pin to call a handler function
//---------------------------------------------------------------------------
static int hardware_set_gpio_input_irq(int pin_number,
                                       int pullup_state,
                                       irqreturn_t (*handler)(int, void *, struct pt_regs *),
                                       const char * dev_name)
    {
    int status = 0;

    // Configure position gpios for input and deglitch for interrupts
    at91_set_gpio_input(pin_number, pullup_state);
    at91_set_deglitch(pin_number, TRUE);

    // Set up interrupt
    status = request_irq(pin_number, (void*)handler, 0, dev_name, NULL);
    if (status != 0)
        {
        if (status == -EINVAL)
            {
           delay_printk(KERN_ERR "request_irq() failed - invalid irq number (0x%08X) or handler\n", pin_number);
            }
        else if (status == -EBUSY)
            {
           delay_printk(KERN_ERR "request_irq(): irq number (0x%08X) is busy, change your config\n", pin_number);
            }
        return FALSE;
        }

    return TRUE;
    }

//---------------------------------------------------------------------------
// Initialize the timer counter as PWM output for motor control
//---------------------------------------------------------------------------
static void hardware_pwm_exit(void) {

   // disable clock registers
   __raw_writel(ATMEL_TC_CLKDIS, tc->regs + ATMEL_TC_REG(CHANNEL_A, CCR));

   // disable specific interrupts for the encoder A
   __raw_writel(ATMEL_TC_COVFS,                           // interrupt on counter overflow
                tc->regs + ATMEL_TC_REG(CHANNEL_A, IDR)); // interrupt disable register

   // free softwrae irqs
   free_irq(tc->irq[CHANNEL_A], NULL);

   // free pin irqs
   free_irq(NCHS_ROD_A_LOWER, NULL);
   free_irq(NCHS_ROD_A_UPPER, NULL);
   free_irq(NCHS_ROD_B_LOWER, NULL);
   free_irq(NCHS_ROD_B_UPPER, NULL);

   // disable clocks
   clk_disable(tc->clk[CHANNEL_A]);

   // free timer counter
   target_timer_free(tc);
}

//---------------------------------------------------------------------------
// Initialize the timer counter as PWM output for motor control
//---------------------------------------------------------------------------
static int hardware_pwm_init(void) {
   int status = 0;

   // initialize timer counter
   tc = target_timer_alloc(NCHS_BLOCK, "gen_tc");
   delay_printk("timer_alloc(): %08x\n", (unsigned int) tc);

   if (!tc) {
      return -EINVAL;
   }

   // enable tc clock
   if (clk_enable(tc->clk[CHANNEL_A]) != 0) {
      delay_printk(KERN_ERR "ENCODER A clk_enable() failed\n");
      return -EINVAL;
   }

   // initialize enc a timer (don't need both a and b timing since we're not interrupting on pins
   __raw_writel(ATMEL_TC_TIMER_CLOCK2,                      // Master clock/4 = 132MHz/4 ~ 33MHz
                tc->regs + ATMEL_TC_REG(CHANNEL_A, CMR));   // channel module register

   // setup encoder A irq
   status = request_irq(tc->irq[CHANNEL_A], (void*)encoder_tc_int, IRQF_DISABLED, "encoder_tc_int", NULL); // timer counter overflow interrupt
   if (status != 0) {
      if (status == -EINVAL) {
         delay_printk(KERN_ERR "request_irq(A): Bad irq number or handler\n");
      } else if (status == -EBUSY) {
         delay_printk(KERN_ERR "request_irq(A): IRQ is busy, change your config\n");
      }
      target_timer_free(tc);
      return status;
   }

   // enable specific interrupts for the encoder A (overflow only)
   __raw_writel(ATMEL_TC_COVFS,                           // interrupt on counter overflow
                tc->regs + ATMEL_TC_REG(CHANNEL_A, IER)); // interrupt enable register
   __raw_writel(ATMEL_TC_TC0XC0S_NONE                     // no signal on XC0
              | ATMEL_TC_TC1XC1S_NONE                     // no signal on XC1
              | ATMEL_TC_TC2XC2S_NONE,                    // no signal on XC2
                tc->regs + ATMEL_TC_REG(CHANNEL_A, BMR)); // block mode register
   __raw_writel(0xffff, tc->regs + ATMEL_TC_REG(CHANNEL_A, RC));


     // initialize clock timers
   __raw_writel(ATMEL_TC_CLKEN, tc->regs + ATMEL_TC_REG(CHANNEL_A, CCR));

   // start input timer
   __raw_writel(ATMEL_TC_SWTRG, tc->regs + ATMEL_TC_REG(CHANNEL_A, CCR));  // control register

   // set to my peripheral
   at91_set_A_periph(NCHS_ROD_A_LOWER, 0);   // TIOA0
   at91_set_B_periph(NCHS_ROD_A_UPPER, 0);    // TIOB0
   at91_set_A_periph(NCHS_ROD_B_LOWER, 0);   // TIOA1
   at91_set_A_periph(NCHS_ROD_B_UPPER, 0);    // TIOB1
   

   // setup interrupts for individual lines (encoder a/b, upper/lower pins)
   if ((hardware_set_gpio_input_irq(NCHS_ROD_A_LOWER, NCHS_PULLUP_STATE, encoder_a_l_int, "encoder_a_l_int") == FALSE) ||
       (hardware_set_gpio_input_irq(NCHS_ROD_A_UPPER, NCHS_PULLUP_STATE, encoder_a_u_int, "encoder_a_u_int") == FALSE) ||
       (hardware_set_gpio_input_irq(NCHS_ROD_B_LOWER, NCHS_PULLUP_STATE, encoder_b_l_int, "encoder_b_l_int") == FALSE) ||
       (hardware_set_gpio_input_irq(NCHS_ROD_B_UPPER, NCHS_PULLUP_STATE, encoder_b_u_int, "encoder_b_u_int") == FALSE)) {
        return FALSE;
   }

   return status;
}



//---------------------------------------------------------------------------
// called on driver shutdown
//---------------------------------------------------------------------------
static int hardware_exit(void) {

   hardware_pwm_exit();

   return 0;
}

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_mover_generic_init(void) {
   int retval;

   // setup PWM input
   retval = hardware_pwm_init();

   // signal that we are fully initialized
   atomic_set(&full_init, TRUE);
   return retval;
}

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_mover_generic_exit(void) {
   atomic_set(&full_init, FALSE);
   del_timer(&encoder_timer_list);
   hardware_exit();
}


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_mover_generic_init);
module_exit(target_mover_generic_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI Non-Contact Hit Sensor module");
MODULE_AUTHOR("ndb");

