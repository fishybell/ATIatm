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
#define ENCODER_DELAY_IN_MSECONDS 25

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
atomic_t lock_al = ATOMIC_INIT(-1); // locking out A, Lower
atomic_t lock_au = ATOMIC_INIT(-1); // locking out A, Upper
atomic_t lock_bl = ATOMIC_INIT(-1); // locking out B, Lower
atomic_t lock_bu = ATOMIC_INIT(-1); // locking out B, Upper

//---------------------------------------------------------------------------
// an encoder pin (A/B, upper/lower) tripped
//---------------------------------------------------------------------------
void encoder_pin(atomic_t *counter, atomic_t *lock, u32 count) {
   // run through the needed steps for when one of the four pins trips
printk("here 6\n");
   if (atomic_inc_return(lock) == 0) { // test if the lock was open
      // TODO -- calculate from last time and overflow
      atomic_set(counter, count);
      mod_timer(&encoder_timer_list, jiffies+((ENCODER_DELAY_IN_MSECONDS*HZ)/1000)); // show values later
   } else {
      atomic_dec(lock); // lock was closed
   }
}

//---------------------------------------------------------------------------
// an encoder pin (rod A or B) tripped
//---------------------------------------------------------------------------
irqreturn_t encoder_int(int channel) {
   atomic_t *count_l, *lock_l; // lower counter/lock
   atomic_t *count_u, *lock_u; // upper counter/lock
   u32 status, this_l, this_u;
//printk("here 5\n");

printk(".");
   if (!atomic_read(&full_init)) {
      return IRQ_HANDLED;
   }
   if (!tc) return IRQ_HANDLED;
printk(",");


   // user appropriate counters/locks
   if (channel == CHANNEL_A) {
      count_l = &count_al;
      lock_l = &lock_al;
      count_u = &count_au;
      lock_u = &lock_au;
   } else if (channel == CHANNEL_B) {
      count_l = &count_bl;
      lock_l = &lock_bl;
      count_u = &count_bu;
      lock_u = &lock_bu;
   } else {
      return IRQ_HANDLED;
   }

   // read registers
   status = __raw_readl(tc->regs + ATMEL_TC_REG(channel, SR)); // status register
   this_l = __raw_readl(tc->regs + ATMEL_TC_REG(channel, RA)); // lower
   this_u = __raw_readl(tc->regs + ATMEL_TC_REG(channel, RB)); // upper

   // Overlflow caused IRQ?
   if ( status & ATMEL_TC_COVFS ) {
      // TODO -- overflow work
printk("O");
   }

   // Pin A (lower) going high caused IRQ?
   if ( status & ATMEL_TC_LDRAS ) {
printk("A");
      encoder_pin(count_l, lock_l, this_l);
   }

   // Pin B (upper) going high caused IRQ?
   if ( status & ATMEL_TC_LDRBS ) {
printk("B");
      encoder_pin(count_u, lock_u, this_u);
   }

   return IRQ_HANDLED;
}

//---------------------------------------------------------------------------
// an encoder A pin tripped
//---------------------------------------------------------------------------
irqreturn_t encoder_a_int(int irq, void *dev_id, struct pt_regs *regs) {
printk("A%i", irq);
   return encoder_int(CHANNEL_A);
}

//---------------------------------------------------------------------------
// an encoder B pin tripped
//---------------------------------------------------------------------------
irqreturn_t encoder_b_int(int irq, void *dev_id, struct pt_regs *regs) {
printk("B%i", irq);
   return encoder_int(CHANNEL_B);
}

//---------------------------------------------------------------------------
// The function that gets called when the encoder timer fires.
//---------------------------------------------------------------------------
static void encoder_fire(unsigned long data) {
   if (!atomic_read(&full_init)) {
      return;
   }

   // print read values
   printk("Al: %i\tAu: %i\tBl: %i\tBu: %i\n",
                atomic_read(&count_al),
                atomic_read(&count_au),
                atomic_read(&count_bl),
                atomic_read(&count_bu));

   // unlock
   atomic_dec(&lock_al);
   atomic_dec(&lock_au);
   atomic_dec(&lock_bl);
   atomic_dec(&lock_bu);
}

//---------------------------------------------------------------------------
// set PINs to be used by peripheral A and/or B
//---------------------------------------------------------------------------
static void init_enc_pins(int channel) {
   #if NCHS_BLOCK == 0
      if (channel == 0) {
         at91_set_A_periph(AT91_PIN_PA26, 0);   // TIOA0
         at91_set_gpio_input(AT91_PIN_PA26, 1); // TIOA0
         at91_set_B_periph(AT91_PIN_PC9, 0);    // TIOB0
         at91_set_gpio_input(AT91_PIN_PC9, 1);  // TIOB0
      } else if (channel == 1) {
         at91_set_A_periph(AT91_PIN_PA27, 0);   // TIOA1
         at91_set_gpio_input(AT91_PIN_PA27, 1); // TIOA1
         at91_set_A_periph(AT91_PIN_PC7, 0);    // TIOB1
         at91_set_gpio_input(AT91_PIN_PC7, 1);  // TIOB1
      } else if (channel == 2) {
         at91_set_A_periph(AT91_PIN_PA28, 0);   // TIOA2
         at91_set_gpio_input(AT91_PIN_PA28, 1); // TIOA2
         at91_set_A_periph(AT91_PIN_PC6, 0);    // TIOB2
         at91_set_gpio_input(AT91_PIN_PC6, 1);  // TIOB2
      }
   #else
      if (channel == 0) {
         at91_set_B_periph(AT91_PIN_PB0, 0);    // TIOA3
         at91_set_gpio_input(AT91_PIN_PB0, 1);  // TIOA3
         at91_set_B_periph(AT91_PIN_PB1, 0);    // TIOB3
         at91_set_gpio_input(AT91_PIN_PB1, 1);  // TIOB3
      } else if (channel == 1) {
         at91_set_B_periph(AT91_PIN_PB2, 0);    // TIOA4
         at91_set_gpio_input(AT91_PIN_PB2, 1);  // TIOA4
         at91_set_B_periph(AT91_PIN_PB18, 0);   // TIOB4
         at91_set_gpio_input(AT91_PIN_PB18, 1); // TIOB4
      } else if (channel == 2) {
         at91_set_B_periph(AT91_PIN_PB3, 0);    // TIOA5
         at91_set_gpio_input(AT91_PIN_PB3, 1);  // TIOA5
         at91_set_B_periph(AT91_PIN_PB19, 0);   // TIOB5
         at91_set_gpio_input(AT91_PIN_PB19, 1); // TIOB5
      }
   #endif
}

//---------------------------------------------------------------------------
// Initialize the timer counter as PWM output for motor control
//---------------------------------------------------------------------------
static void hardware_pwm_exit(void) {

   // disable clock registers
   __raw_writel(ATMEL_TC_CLKEN, tc->regs + ATMEL_TC_REG(CHANNEL_A, CCR));
   __raw_writel(ATMEL_TC_CLKEN, tc->regs + ATMEL_TC_REG(CHANNEL_B, CCR));

   // disable specific interrupts for the encoder A
   __raw_writel(ATMEL_TC_COVFS                            // interrupt on counter overflow
              | ATMEL_TC_LDRAS                            // interrupt on loading RA
              | ATMEL_TC_LDRBS,                           // interrupt on loading RB
                tc->regs + ATMEL_TC_REG(CHANNEL_A, IDR)); // interrupt disable register

   // disable specific interrupts for the encoder B
   __raw_writel(ATMEL_TC_COVFS                            // interrupt on counter overflow
              | ATMEL_TC_LDRAS                            // interrupt on loading RA
              | ATMEL_TC_LDRBS,                           // interrupt on loading RB
                tc->regs + ATMEL_TC_REG(CHANNEL_B, IDR)); // interrupt disable register

   // free softwrae irqs
   free_irq(tc->irq[CHANNEL_A], NULL);
   free_irq(tc->irq[CHANNEL_B], NULL);

   // disable clocks
   clk_disable(tc->clk[CHANNEL_A]);
   clk_disable(tc->clk[CHANNEL_B]);

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
   printk("timer_alloc(): %08x\n", (unsigned int) tc);

   if (!tc) {
      return -EINVAL;
   }


   // initialize encoder pins
   init_enc_pins(CHANNEL_A);
   init_enc_pins(CHANNEL_B);

   if (clk_enable(tc->clk[CHANNEL_A]) != 0) {
      printk(KERN_ERR "ENCODER A clk_enable() failed\n");
      return -EINVAL;
   }

   if (clk_enable(tc->clk[CHANNEL_B]) != 0) {
       printk(KERN_ERR "ENCODER B clk_enable() failed\n");
       return -EINVAL;
   }

   // initialize enc a timer
   __raw_writel(ATMEL_TC_TIMER_CLOCK5                       // slow clock 33khz -- // Master clock/2 = 132MHz/2 ~ 66MHz
              | ATMEL_TC_ETRGEDG_RISING                     // Trigger on the rising and falling edges
              | ATMEL_TC_ABETRG                             // Trigger on TIOA or TIOB
              | ATMEL_TC_LDRA_RISING                        // Load RA on rising edge
              | ATMEL_TC_LDRB_RISING,                       // Load RB on rising edge
                tc->regs + ATMEL_TC_REG(CHANNEL_A, CMR));   // channel module register

   // initialize enc b timer
   __raw_writel(ATMEL_TC_TIMER_CLOCK5                       // slow clock 33khz -- // Master clock/2 = 132MHz/2 ~ 66MHz
              | ATMEL_TC_ETRGEDG_RISING                     // Trigger on the rising and falling edges
              | ATMEL_TC_ABETRG                             // Trigger on TIOA or TIOB
              | ATMEL_TC_LDRA_RISING                        // Load RA on rising edge
              | ATMEL_TC_LDRB_RISING,                       // Load RB on rising edge
                tc->regs + ATMEL_TC_REG(CHANNEL_B, CMR));   // channel module register

   // setup encoder A irq
printk("going to request irq A: %i\n", tc->irq[CHANNEL_A]);
   status = request_irq(tc->irq[CHANNEL_A], (void*)encoder_a_int, IRQF_DISABLED, "encoder_a_int", NULL);
printk("requested irq A: %i\n", status);
   if (status != 0) {
      if (status == -EINVAL) {
         printk(KERN_ERR "request_irq(A): Bad irq number or handler\n");
      } else if (status == -EBUSY) {
         printk(KERN_ERR "request_irq(A): IRQ is busy, change your config\n");
      }
      target_timer_free(tc);
      return status;
   }

   // setup encoder B irq
printk("going to request irq B: %i\n", tc->irq[CHANNEL_B]);
   status = request_irq(tc->irq[CHANNEL_B], (void*)encoder_b_int, IRQF_DISABLED, "encoder_b_int", NULL);
printk("requested irq B: %i\n", status);
   if (status != 0) {
      if (status == -EINVAL) {
         printk(KERN_ERR "request_irq(B): Bad irq number or handler\n");
      } else if (status == -EBUSY) {
         printk(KERN_ERR "request_irq(B): IRQ is busy, change your config\n");
      }
      target_timer_free(tc);
      return status;
   }

   // enable specific interrupts for the encoder A
   __raw_writel(ATMEL_TC_COVFS                            // interrupt on counter overflow
              | ATMEL_TC_LDRAS                            // interrupt on loading RA
              | ATMEL_TC_LDRBS,                           // interrupt on loading RB
                tc->regs + ATMEL_TC_REG(CHANNEL_A, IER)); // interrupt enable register
   __raw_writel(ATMEL_TC_TC0XC0S_NONE                     // no signal on XC0
              | ATMEL_TC_TC1XC1S_NONE                     // no signal on XC1
              | ATMEL_TC_TC2XC2S_NONE,                    // no signal on XC2
                tc->regs + ATMEL_TC_REG(CHANNEL_A, BMR)); // block mode register
   __raw_writel(0xffff, tc->regs + ATMEL_TC_REG(CHANNEL_A, RC));

printk("bytes written: %04x\n", __raw_readl(tc->regs + ATMEL_TC_REG(CHANNEL_A, IMR)));

   // enable specific interrupts for the encoder B
   __raw_writel(ATMEL_TC_COVFS                            // interrupt on counter overflow
              | ATMEL_TC_LDRAS                            // interrupt on loading RA
              | ATMEL_TC_LDRBS,                           // interrupt on loading RB
                tc->regs + ATMEL_TC_REG(CHANNEL_B, IER)); // interrupt enable register
   __raw_writel(ATMEL_TC_TC0XC0S_NONE                     // no signal on XC0
              | ATMEL_TC_TC1XC1S_NONE                     // no signal on XC1
              | ATMEL_TC_TC2XC2S_NONE,                    // no signal on XC2
                tc->regs + ATMEL_TC_REG(CHANNEL_B, BMR)); // block mode register
   __raw_writel(0xffff, tc->regs + ATMEL_TC_REG(CHANNEL_B, RC));

printk("bytes written: %04x\n", __raw_readl(tc->regs + ATMEL_TC_REG(CHANNEL_B, IMR)));

     // initialize clock timers
printk("here 0.1\n");
   __raw_writel(ATMEL_TC_CLKEN, tc->regs + ATMEL_TC_REG(CHANNEL_A, CCR));
printk("here 0.2\n");
   __raw_writel(ATMEL_TC_CLKEN, tc->regs + ATMEL_TC_REG(CHANNEL_B, CCR));
//printk("here 0.3\n");
//   __raw_writel(ATMEL_TC_SYNC, tc->regs + ATMEL_TC_BCR);
printk("here 1\n");

   // stat input timer
   __raw_writel(ATMEL_TC_SWTRG, tc->regs + ATMEL_TC_REG(CHANNEL_A, CCR));  // control register
printk("here 1.1\n");
   __raw_writel(ATMEL_TC_SWTRG, tc->regs + ATMEL_TC_REG(CHANNEL_B, CCR));  // control register
printk("here 2\n");

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
printk("here 0\n");
   retval = hardware_pwm_init();

   // signal that we are fully initialized
printk("here 3\n");
   atomic_set(&full_init, TRUE);
printk("here 4\n");
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

