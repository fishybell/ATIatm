//---------------------------------------------------------------------------
// target_mover_position.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>
#include <linux/atmel_tc.h>
#include <linux/clk.h>


#include "target.h"
#include "target_hardware.h"
#include "target_mover_position.h"
//---------------------------------------------------------------------------


#define TARGET_NAME  "Mover Position"

#define PWM_BLOCK		1		// block 0 : TIOA0-2, TIOB0-2 , block 1 : TIOA3-5, TIOB3-5 
#define PWM_CHANNEL		0		// channel 0 matches TIOA0 to TIOB0, same for 1 and 2

#define MAX_TIME	0x10000
#define MAX_OVER	0x10000
#define VELO_K		0x1000000

#define TICKS_PER_LEG 100

//#define TESTING_ON_EVAL
#ifdef TESTING_ON_EVAL

#undef INPUT_MOVER_TRACK_SENSOR_ACTIVE_STATE
#undef INPUT_MOVER_TRACK_SENSOR_PULLUP_STATE
#undef INPUT_MOVER_TRACK_SENSOR_1
#undef INPUT_MOVER_TRACK_SENSOR_2

#define INPUT_MOVER_TRACK_SENSOR_ACTIVE_STATE           ACTIVE_LOW
#define INPUT_MOVER_TRACK_SENSOR_PULLUP_STATE           PULLUP_ON
#define INPUT_MOVER_TRACK_SENSOR_1                                      AT91_PIN_PA30
#define INPUT_MOVER_TRACK_SENSOR_2                                      AT91_PIN_PA31

#endif

struct atmel_tc *tc;

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// These atomic variables is use to indicate global changes
//---------------------------------------------------------------------------
atomic_t velocity = ATOMIC_INIT(0);
atomic_t last_t = ATOMIC_INIT(0);
atomic_t o_count = ATOMIC_INIT(0);
atomic_t delta_t = ATOMIC_INIT(0);
atomic_t position = ATOMIC_INIT(0);
atomic_t position_old = ATOMIC_INIT(0);
atomic_t legs = ATOMIC_INIT(0);
atomic_t direction = ATOMIC_INIT(0);


//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space that the bit
// button has been pressed.
//---------------------------------------------------------------------------
static struct work_struct position_work;
static struct work_struct velocity_work;
static struct work_struct delta_work;

//---------------------------------------------------------------------------
// passed a track sensor
//---------------------------------------------------------------------------
irqreturn_t track_sensor_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    int dir;
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }
    // only handle the interrupt when sensor 1 is active
    if (at91_get_gpio_value(INPUT_MOVER_TRACK_SENSOR_1) == INPUT_MOVER_TRACK_SENSOR_ACTIVE_STATE)
        {
        // is the sensor 2 active or not?
        if (at91_get_gpio_value(INPUT_MOVER_TRACK_SENSOR_2) == INPUT_MOVER_TRACK_SENSOR_ACTIVE_STATE)
            {
            // both active, going forward
            dir = 1;
            }
        else
            {
            // only sensor 1 is active, going backward
            dir = -1;
            }
        // calculate position based on number of times we've passed a track leg
        atomic_set(&legs, atomic_read(&legs) + dir); // gain a leg or lose a leg
        atomic_set(&position, atomic_read(&legs) * TICKS_PER_LEG); // this overwrites the value received from the quad encoder
        schedule_work(&position_work); // notify the system
        }
    }

//---------------------------------------------------------------------------
// the timer triggered
//---------------------------------------------------------------------------
irqreturn_t target_mover_position_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    u32 status, rb, cv, this_t;
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }
    if (!tc) return IRQ_HANDLED;
    status = __raw_readl(tc->regs + ATMEL_TC_REG(PWM_CHANNEL, SR)); /* status register */
    this_t = __raw_readl(tc->regs + ATMEL_TC_REG(PWM_CHANNEL, RA));
    rb = __raw_readl(tc->regs + ATMEL_TC_REG(PWM_CHANNEL, RB));
    cv = __raw_readl(tc->regs + ATMEL_TC_REG(PWM_CHANNEL, CV));

printk(KERN_ALERT "O:%i A:%i l:%i t:%i c:%i o:%08x\n", status & ATMEL_TC_COVFS, status & ATMEL_TC_LDRAS, atomic_read(&last_t), this_t, cv, atomic_read(&o_count) );

    /* Overlflow caused IRQ? */
    if ( status & ATMEL_TC_COVFS )
        {
        atomic_set(&o_count, atomic_read(&o_count) + MAX_TIME);
        }

    /* Pin A going high caused IRQ? */
    if ( status & ATMEL_TC_LDRAS )
        {
        /* change position */
        if ( status & ATMEL_TC_MTIOB )
            {
            atomic_set(&position, atomic_read(&position) - 1);
            atomic_set(&direction, -1);
            }
        else
            {
            atomic_set(&position, atomic_read(&position) + 1);
            atomic_set(&direction, 1);
            }
        atomic_set(&delta_t, this_t + atomic_read(&o_count));
        atomic_set(&o_count, 0);
        atomic_set(&velocity, VELO_K / atomic_read(&delta_t) * atomic_read(&direction));
        atomic_set(&last_t, this_t);
        schedule_work(&position_work);
        schedule_work(&delta_work);
        schedule_work(&velocity_work);
        }
    else
        {
        /* Pin A did not go high */
        if ( atomic_read(&o_count) > MAX_OVER )
            {
            atomic_set(&velocity, 0);
            schedule_work(&velocity_work);
            }
        }

    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
// set PINs to be used by peripheral A and/or B
//---------------------------------------------------------------------------
static void init_pins()
    {
    #if PWM_BLOCK == 0
        #if PWM_CHANNEL == 0
            at91_set_A_periph(AT91_PIN_PA26, 0);	/* TIOA0 */
            at91_set_gpio_input(AT91_PIN_PA26, 1);	/* TIOA0 */
            at91_set_B_periph(AT91_PIN_PC9, 0);		/* TIOB0 */
            at91_set_gpio_input(AT91_PIN_PC9, 1);	/* TIOB0 */
        #elif PWM_CHANNEL == 1
            at91_set_A_periph(AT91_PIN_PA27, 0);	/* TIOA1 */
            at91_set_gpio_input(AT91_PIN_PA27, 1);	/* TIOA1 */
            at91_set_A_periph(AT91_PIN_PC7, 0);		/* TIOB1 */
            at91_set_gpio_input(AT91_PIN_PC7, 1);	/* TIOB1 */
        #elif PWM_CHANNEL == 2
            at91_set_A_periph(AT91_PIN_PA28, 0);	/* TIOA2 */
            at91_set_gpio_input(AT91_PIN_PA28, 1);	/* TIOA2 */
            at91_set_A_periph(AT91_PIN_PC6, 0);		/* TIOB2 */
            at91_set_gpio_input(AT91_PIN_PC6, 1);	/* TIOB2 */
        #endif
    #else
        #if PWM_CHANNEL == 0
            at91_set_B_periph(AT91_PIN_PB0, 0);		/* TIOA3 */
            at91_set_gpio_input(AT91_PIN_PB0, 1);	/* TIOA3 */
            at91_set_B_periph(AT91_PIN_PB1, 0);		/* TIOB3 */
            at91_set_gpio_input(AT91_PIN_PB1, 1);	/* TIOB3 */
        #elif PWM_CHANNEL == 1
            at91_set_B_periph(AT91_PIN_PB2, 0);		/* TIOA4 */
            at91_set_gpio_input(AT91_PIN_PB2, 1);	/* TIOA4 */
            at91_set_B_periph(AT91_PIN_PB18, 0);	/* TIOB4 */
            at91_set_gpio_input(AT91_PIN_PB18, 1);	/* TIOB4 */
        #elif PWM_CHANNEL == 2
            at91_set_B_periph(AT91_PIN_PB3, 0);		/* TIOA5 */
            at91_set_gpio_input(AT91_PIN_PB3, 1);	/* TIOA5 */
            at91_set_B_periph(AT91_PIN_PB19, 0);	/* TIOB5 */
            at91_set_gpio_input(AT91_PIN_PB19, 1);	/* TIOB5 */
        #endif
    #endif
    }
//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0;
printk(KERN_ALERT "initializing clock\n");

    // initialize timer
    tc = target_timer_alloc(PWM_BLOCK, "gen_tc");
    printk(KERN_ALERT "target_timer_alloc(): %08x\n", (unsigned int) tc);

    if (!tc) return -EINVAL;

    init_pins();
    clk_enable(tc->clk[PWM_CHANNEL]);

printk(KERN_ALERT "clock enabled\n");

    /* initialize counter */
    __raw_writel(ATMEL_TC_TIMER_CLOCK5				/* Master clock / 128 : 66 mhz */
                    | ATMEL_TC_ETRGEDG_RISING			/* Trigger on the rising and falling edges */
                    | ATMEL_TC_ABETRG				/* Trigger on TIOA */
                    | ATMEL_TC_LDRA_RISING,			/* Load RA on rising edge */
                    tc->regs + ATMEL_TC_REG(PWM_CHANNEL, CMR));	/* channel module register */

    /* disable all interrupts, then enable specific interrupts */
//    __raw_writel(0xff, tc->regs + ATMEL_TC_REG(PWM_CHANNEL, IDR));		/* irq disable register */
    __raw_writel(ATMEL_TC_COVFS					/* interrupt on counter overflow */
                    | ATMEL_TC_LDRAS,				/* interrupt on loading RA */
                    tc->regs + ATMEL_TC_REG(PWM_CHANNEL, IER)); /* interrupt enable register*/
    __raw_writel(ATMEL_TC_TC0XC0S_NONE				/* no signal on XC0 */
                    | ATMEL_TC_TC1XC1S_NONE			/* no signal on XC1 */
                    | ATMEL_TC_TC2XC2S_NONE,			/* no signal on XC2 */
                    tc->regs + ATMEL_TC_REG(PWM_CHANNEL, BMR)); /* block mode register*/
    __raw_writel(0xffff, tc->regs + ATMEL_TC_REG(PWM_CHANNEL, RC));

printk(KERN_ALERT "bytes written: %04x\n", __raw_readl(tc->regs + ATMEL_TC_REG(PWM_CHANNEL, IMR)));

    /* setup system irq */
    status = request_irq(tc->irq[PWM_CHANNEL], (void*)target_mover_position_int, 0, "target_mover_position_int", NULL);
printk(KERN_ALERT "requested irq: %i\n", status);
    if (status != 0)
        {
        if (status == -EINVAL)
            {
            printk(KERN_ERR "request_irq(): Bad irq number or handler\n");
            }
        else if (status == -EBUSY)
            {
            printk(KERN_ERR "request_irq(): IRQ is busy, change your config\n");
            }
        target_timer_free(tc);
        return status;
        }
   
printk(KERN_ALERT "moving on\n");


    /* enable, sync clock */
    __raw_writel(ATMEL_TC_CLKEN, tc->regs + ATMEL_TC_REG(PWM_CHANNEL, CCR));	/* channel control register */
    __raw_writel(ATMEL_TC_SYNC, tc->regs + ATMEL_TC_BCR);			/* block control register */

    /* start timer */
    __raw_writel(ATMEL_TC_SWTRG, tc->regs + ATMEL_TC_REG(PWM_CHANNEL, CCR));	/* control register */

    // Configure track sensor gpios for input and deglitch for interrupts
    at91_set_gpio_input(INPUT_MOVER_TRACK_SENSOR_1, INPUT_MOVER_TRACK_SENSOR_PULLUP_STATE);
    at91_set_gpio_input(INPUT_MOVER_TRACK_SENSOR_2, INPUT_MOVER_TRACK_SENSOR_PULLUP_STATE);
    at91_set_deglitch(INPUT_MOVER_TRACK_SENSOR_1, INPUT_MOVER_TRACK_SENSOR_DEGLITCH_STATE);

    status = request_irq(INPUT_MOVER_TRACK_SENSOR_1, (void*)track_sensor_int, 0, "user_interface_bit_button", NULL);
    if (status != 0)
        {
        if (status == -EINVAL)
            {
                printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", INPUT_MOVER_TRACK_SENSOR_1);
            }
        else if (status == -EBUSY)
            {
                printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", INPUT_MOVER_TRACK_SENSOR_1);
            }

        return status;
        }

printk(KERN_ALERT "done\n");
    return status;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_exit(void)
    {
	free_irq(tc->irq[PWM_CHANNEL], NULL);
        target_timer_free(tc);
	return 0;
    }

//---------------------------------------------------------------------------
// Handles reads to the position attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t position_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%i\n", atomic_read(&position));
    }

// Handles reads to the velocity attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t velocity_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%i\n", atomic_read(&velocity));
    }

// Handles reads to the delta attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t delta_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%i\n", atomic_read(&delta_t));
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(position, 0444, position_show, NULL);
static DEVICE_ATTR(velocity, 0444, velocity_show, NULL);
static DEVICE_ATTR(delta, 0444, delta_show, NULL);

//---------------------------------------------------------------------------
// Defines the attributes of the pwm timer for sysfs
//---------------------------------------------------------------------------
static const struct attribute * target_mover_position_attrs[] =
    {
    &dev_attr_position.attr,
    &dev_attr_velocity.attr,
    &dev_attr_delta.attr,
    NULL,
    };

//---------------------------------------------------------------------------
// Defines the attribute group of the pwm timer for sysfs
//---------------------------------------------------------------------------
const struct attribute_group target_mover_position_attr_group =
    {
    .attrs = (struct attribute **) target_mover_position_attrs,
    };

//---------------------------------------------------------------------------
// Returns the attribute group of the pwm timer
//---------------------------------------------------------------------------
const struct attribute_group * target_mover_position_get_attr_group(void)
    {
    return &target_mover_position_attr_group;
    }

//---------------------------------------------------------------------------
// declares the target_device that is added/removed through target.ko
//---------------------------------------------------------------------------
struct target_device target_device_mover_position =
    {
    .type     		= TARGET_TYPE_MOVER_POSITION,
    .name     		= TARGET_NAME,
    .dev     		= NULL,
    .get_attr_group	= target_mover_position_get_attr_group,
    };

//---------------------------------------------------------------------------
// Work item to notify the user-space about a change in a variable
//---------------------------------------------------------------------------
static void do_position(struct work_struct * work)
        {
        if (abs(atomic_read(&position_old) - atomic_read(&position)) > (TICKS_PER_LEG/2))
            {
            atomic_set(&position_old, atomic_read(&position)); 
            target_sysfs_notify(&target_device_mover_position, "position");
            }
        }

static void do_velocity(struct work_struct * work)
        {
        target_sysfs_notify(&target_device_mover_position, "velocity");
        }

static void do_delta(struct work_struct * work)
        {
        target_sysfs_notify(&target_device_mover_position, "delta");
        }

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_mover_position_init(void)
    {
    int retval;
    printk(KERN_ALERT "%s(): %s - %s\n",__func__,  __DATE__, __TIME__);

    hardware_init();

    INIT_WORK(&position_work, do_position);
    INIT_WORK(&velocity_work, do_velocity);
    INIT_WORK(&delta_work, do_delta);

    retval= target_sysfs_add(&target_device_mover_position);

    // signal that we are fully initialized
    atomic_set(&full_init, TRUE);
    return retval;

    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_mover_position_exit(void)
    {
	hardware_exit();
    target_sysfs_remove(&target_device_mover_position);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_mover_position_init);
module_exit(target_mover_position_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI pwm timer module");
MODULE_AUTHOR("jpy");

