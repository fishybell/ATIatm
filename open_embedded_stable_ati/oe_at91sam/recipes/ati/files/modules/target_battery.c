//---------------------------------------------------------------------------
// target_battery.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <mach/gpio.h>

#include "netlink_kernel.h"

#include "target.h"
#include "target_battery.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"battery"

// TODO - the ADC code could be broken into its own file to allows configuration
// and use of more than one ADC channel at a time.

// various timer timeouts
#define TIMEOUT_IN_SECONDS		10
#define ADC_READ_DELAY_IN_MSECONDS	20
#define LED_BLINK_ON_IN_MSECONDS	100
#define LED_BLINK_COUNT			3
#define LED_BLINK_OFF_IN_MSECONDS	1500
#define LED_CHARGING_ON_IN_MSECONDS	500
#define LED_CHARGING_OFF_IN_MSECONDS	500
#define SHUTDOWN_IN_MSECONDS		250
#define REENABLE_IN_SECONDS 20

#define BATTERY_CHARGING_NO   		0
#define BATTERY_CHARGING_YES    	1
#define BATTERY_CHARGING_ERROR  	2


//#define TESTING_ON_EVAL
#ifdef TESTING_ON_EVAL
	#undef	INPUT_ADC_LOW_BAT

	#undef OUTPUT_LED_LOW_BAT_ACTIVE_STATE
	#undef	OUTPUT_LED_LOW_BAT

	#undef INPUT_CHARGING_BAT_ACTIVE_STATE
	#undef INPUT_CHARGING_BAT_PULLUP_STATE
	#undef INPUT_CHARGING_BAT_DEGLITCH_STATE
	#undef INPUT_CHARGING_BAT

	#undef USB_ENABLE

	#undef TIMEOUT_IN_SECONDS
	#define TIMEOUT_IN_SECONDS		5
	#define	INPUT_ADC_LOW_BAT 						AT91_PIN_PC0

	#define OUTPUT_LED_LOW_BAT_ACTIVE_STATE			ACTIVE_LOW
	#define	OUTPUT_LED_LOW_BAT 						AT91_PIN_PB8 //PA6 LED on dev. board
	#define	OUTPUT_LED_TEST 						AT91_PIN_PB9 //power LED on dev. board

	#define INPUT_CHARGING_BAT_ACTIVE_STATE			ACTIVE_LOW
	#define INPUT_CHARGING_BAT_PULLUP_STATE			PULLUP_ON
	#define INPUT_CHARGING_BAT_DEGLITCH_STATE		DEGLITCH_ON
	#define	INPUT_CHARGING_BAT					AT91_PIN_PA31 // PB3 on dev. board

	#define USB_ENABLE					AT91_PIN_PC5
#endif // TESTING_ON_EVAL

#if 	(INPUT_ADC_LOW_BAT == AT91_PIN_PC0)
	#define ADC_CHAN 	0
#elif 	(INPUT_ADC_LOW_BAT == AT91_PIN_PC1)
	#define ADC_CHAN 	1
#elif 	(INPUT_ADC_LOW_BAT == AT91_PIN_PC2)
	#define ADC_CHAN 	2
#elif 	(INPUT_ADC_LOW_BAT == AT91_PIN_PC3)
	#define ADC_CHAN 	3
#else
	#error ERROR - INPUT_ADC_LOW_BAT setting
#endif

// ADC memory base and clock
void __iomem *	adc_base;
struct clk *	adc_clk;

#define ADC_CR          0x00    // Control Register Offset
#define ADC_MR          0x04    // Mode Register Offset
#define ADC_CHER        0x10    // Channel Enable Register Offset
#define ADC_CHDR        0x14    // Channel Disable Register Offset
#define ADC_CHSR        0x18    // Channel Status Register Offset
#define ADC_SR          0x1C    // Status Register Offset
#define ADC_LCDR        0x20    // Last Converted Data Register Offset
#define ADC_IER         0x24    // Interrupt Enable Register Offset
#define ADC_IDR         0x28    // Interrupt Disable Register Offset
#define ADC_IMR         0x2C    // Interrupt Mask Register Offset

/*
#define ADC_CDR0        0x30    // Channel Data Register 0 Offset
#define ADC_CDR1        0x34    // Channel Data Register 1 Offset
#define ADC_CDR2        0x38    // Channel Data Register 2 Offset
#define ADC_CDR3        0x3C    // Channel Data Register 3 Offset

#define CH_EN           0x01    // Channels to Enable - channel 0, this needs to match the pin setting
#define CH_DIS          0x0E    // Channels to Disable - channels 1,2, and 3 will be disabled
*/

#define ADC_CDR        	(0x30+(4*ADC_CHAN))   	// Channel Data Register
#define CH_EN           (0x01<<ADC_CHAN)	 	// Channel to enable
#define CH_DIS          (!CH_EN)				// Channels to disable
#define ADC_START	0x02			// bit in the control register to start the ADC

#define TRGEN           0x00    // 	Hardware triggers are disabled. Starting a conversion is only possible by software.
#define TRGSEL          0x00    // 	Trigger Select - disregarded, see above
#define LOWRES          0x01    // 	8-bit resolution
#define SLEEP_MODE      0x00    // 	Normal Mode
#define PRESCAL         0x9   	//	Prescaler Rate Selection
#define STARTUP         0x7    	//	Start Up Time
#define SHTIM           0x3    	//	Sample and Hold Time
#define ADC_MODE (SHTIM << 24 | STARTUP << 16 | PRESCAL << 8 | SLEEP_MODE << 5 | LOWRES << 4 | TRGSEL << 1 | TRGEN)

// NOTE: The ADC settings need to agree on the PIN, channels to enable and disable, and the channel to read
// TODO - automate ADC settings via #define or otherwise

//---------------------------------------------------------------------------
// These variables are parameters giving when doing an insmod (insmod blah.ko variable=5)
//---------------------------------------------------------------------------
static int charge = FALSE;
module_param(charge, bool, S_IRUGO);
static int shutdown = TRUE;
module_param(shutdown, bool, S_IRUGO);
static int shutdown_wait = 1;
module_param(shutdown_wait, int, S_IRUGO);
static int minvoltval = 12;
module_param(minvoltval, int, S_IRUGO);

// these lists go 12, 24, 36, 48, error
const int BATTERY_NORMAL[] = {45, 90, 135, 180, 255};	// at or above this value is considered normal
const int BATTERY_LOW[] =    {42, 84, 126, 168, 4};		// at or above this value (and below above) is considered low
const int BATTERY_CRIT[] =   {41, 82, 123, 164, 3};		// at or above this value (and below above) is considered critical
const int BATTERY_HALT[] =   {40, 80, 120, 160, 2};		// at or below this value (and above below) requires immediate halt of device -- should be around 10v, 20v, 30v, 40v
const int BATTERY_WAIT[] =   {5, 5, 5, 5, 1};			// at or below this value signifies an invalid battery value...ignore

//---------------------------------------------------------------------------
// Declaration of the functions that gets called when the timers fire.
//---------------------------------------------------------------------------
static void timeout_fire(unsigned long data);
static void adc_read_fire(unsigned long data);
static void led_blink_fire(unsigned long data);
static void charging_fire(unsigned long data);
static void shutdown_device(unsigned long data);
static void reenable_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timers for the timeout, adc read delay, and various led blinks
//---------------------------------------------------------------------------
static struct timer_list timeout_timer_list = TIMER_INITIALIZER(timeout_fire, 0, 0);
static struct timer_list adc_read_timer_list = TIMER_INITIALIZER(adc_read_fire, 0, 0);
static struct timer_list led_blink_timer_list = TIMER_INITIALIZER(led_blink_fire, 0, 0);
static struct timer_list charging_timer_list = TIMER_INITIALIZER(charging_fire, 0, 0);
static struct timer_list shutdown_timer_list = TIMER_INITIALIZER(shutdown_device, 0, 0);
static struct timer_list reenable_timer_list = TIMER_INITIALIZER(reenable_fire, 0, 0);

//---------------------------------------------------------------------------
// Maps the battery charging state to state name.
//---------------------------------------------------------------------------
static const char * battery_charging_state[] =
    {
    "no",
    "yes",
    "error"
    };

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(FALSE);

//---------------------------------------------------------------------------
// This atomic variable is use to hold our driver id from netlink provider
//---------------------------------------------------------------------------
atomic_t driver_id = ATOMIC_INIT(-1);

//---------------------------------------------------------------------------
// This atomic variable is used for counting shutdown messages
//---------------------------------------------------------------------------
atomic_t shutdown_msg = ATOMIC_INIT(0); // no shutdown messages so far

//---------------------------------------------------------------------------
// This atomic variable is used for battyer enable/disable checking
//---------------------------------------------------------------------------
atomic_t check_atomic = ATOMIC_INIT(1); // enabled

//---------------------------------------------------------------------------
// This atomic variable used for storing the last adc value read
//---------------------------------------------------------------------------
atomic_t adc_atomic = ATOMIC_INIT(255); // max value on startup
atomic_t adc_startup = ATOMIC_INIT(1);  // we're still starting up

//---------------------------------------------------------------------------
// This atomic variable used for storing the amount of times we've blinked
// in one blinking set.
//---------------------------------------------------------------------------
atomic_t blink_count = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space that the adc
// level has changed
//---------------------------------------------------------------------------
static struct work_struct level_work;

//---------------------------------------------------------------------------
// enables/disables battery sensing (so we don't sense while running a moter)
//---------------------------------------------------------------------------
void enable_battery_check(int enable) {
   atomic_set(&check_atomic, enable);
   // automatically reenable the battery check if the calling ko doesn't
   if (enable == 0) {
      mod_timer(&reenable_timer_list, jiffies+(REENABLE_IN_SECONDS*HZ));
   } else {
      del_timer(&reenable_timer_list);
   }
}
EXPORT_SYMBOL(enable_battery_check);

//---------------------------------------------------------------------------
// Shutdown the device
//---------------------------------------------------------------------------
static void shutdown_soon(bool lowbat) {
    u8 data = BATTERY_SHUTDOWN;

    // tell the debug port we're shutting down
    delay_printk("Shutting down due to %s.\n", lowbat ? "low battery" : "user command");

    if (atomic_inc_return(&shutdown_msg) < 3) {
        // let attached devices know that we're shutting down
        queue_nl_multi(NL_C_BATTERY, &data, sizeof(data));

        // on command (and when we're programmed to wait) shutdown after a long delay 
        if (!lowbat || shutdown_wait <= 0) {
            // build shutdown scenario
            char *scen = "\
               {Send;R_LIFTER;NL_C_EXPOSE;1;00} -- Send Conceal to lifter \
               {Send;R_MOVER;NL_C_MOVE;1;0080} -- Send Stop to mover";
            target_kill_scenario(); // abort running scenarios
            target_scenario(scen); // send shutdown scenario

            // shutdown in a long period of time
            mod_timer(&shutdown_timer_list, jiffies+(shutdown_wait*HZ));
        } else {
            // shutdown in a short period of time
            mod_timer(&shutdown_timer_list, jiffies+((SHUTDOWN_IN_MSECONDS*HZ)/1000));
        }
    }
}

static void shutdown_device(unsigned long data) {
    // can't use delay_printk anymore, oh well
    printk("Shutting down now.\n");
    // TODO -- do a proper linux shutdown and have linux assert this line?
    // assert this line to trip the power relay
    at91_set_gpio_output(OUTPUT_POWER_OFF, ACTIVE_LOW);
}

//---------------------------------------------------------------------------
// battery percentage from adc value
//---------------------------------------------------------------------------
int bat_from_adc(int adc_val) {
    // turn an adc_val into a percentage
    if (adc_val >= BATTERY_NORMAL[minvoltval]) {
        adc_val = BAT_NORMAL; // normal percentage
    } else if (adc_val >= BATTERY_LOW[minvoltval]) {
        adc_val = BAT_LOW; // low percentage
    } else if (adc_val >= BATTERY_CRIT[minvoltval]) {
        adc_val = BAT_CRIT; // critical percentage
    } else if (adc_val <= BATTERY_WAIT[minvoltval]) {
        adc_val = BAT_INVALID; // invalid percentage
    } else if (adc_val <= BATTERY_HALT[minvoltval]) {
        adc_val = BAT_HALT; // no battery left
    }
    return adc_val;
}

//---------------------------------------------------------------------------
// Start the ADC reading
//---------------------------------------------------------------------------
static void hardware_adc_read_start(void)
	{
	__raw_writel(ADC_START, adc_base + ADC_CR); // Start the ADC
        mod_timer(&adc_read_timer_list, jiffies+((ADC_READ_DELAY_IN_MSECONDS*HZ)/1000));
	}

//---------------------------------------------------------------------------
// The ADC Read Delay timer fired
//---------------------------------------------------------------------------
static void adc_read_fire(unsigned long data)
	{
	unsigned char adc_val;
	unsigned char old_adc_val;

    // check/don't check adc line
    if (!atomic_read(&check_atomic)) {
        return;
    }

   // we're not starting up anymore
   atomic_set(&adc_startup, 0);

	// check if the register adc register is ready
	if(!(__raw_readl(adc_base + ADC_SR) & CH_EN))
		{
		// try again later
        	mod_timer(&adc_read_timer_list, jiffies+((ADC_READ_DELAY_IN_MSECONDS*HZ)/1000));
		}
	else
		{
		// Read the conversion
		adc_val = __raw_readl(adc_base + ADC_CDR);

		// has the value changed?
		old_adc_val = atomic_read(&adc_atomic);
		if (adc_val != old_adc_val)
			{
			atomic_set(&adc_atomic, adc_val);
            switch (bat_from_adc(adc_val)) {
                case BAT_NORMAL:	delay_printk("ADC = %i (normal)\n", adc_val); break;
                case BAT_LOW:		delay_printk("ADC = %i (low)\n", adc_val); break;
                case BAT_CRIT:		delay_printk("ADC = %i (critical)\n", adc_val); break;
                case BAT_INVALID:	delay_printk("ADC = %i (invalid)\n", adc_val); break;
                case BAT_HALT: 
                   // print out that we would have shutdown and/or shutdown
                   delay_printk("ADC = %i (shutdown)\n", adc_val);
                   if (shutdown) { // only if allowed to shutdown
                      shutdown_soon(true);
                   }
                   break;
            }

			// notify user-space
			schedule_work(&level_work);

			// start blinking if we're too low
			if (adc_val < BATTERY_NORMAL[minvoltval])
				{
        			mod_timer(&led_blink_timer_list, jiffies+((LED_BLINK_ON_IN_MSECONDS*HZ)/1000));
				}
			}
		}
	}

//---------------------------------------------------------------------------
// The function that gets called when the timeout fires.
//---------------------------------------------------------------------------
static void timeout_fire(unsigned long data) {
    // check/don't check adc line
    if (atomic_read(&check_atomic)) {
        // start the adc reading
        hardware_adc_read_start();
    }

    // Restart the timeout timer
    mod_timer(&timeout_timer_list, jiffies+(TIMEOUT_IN_SECONDS*HZ));
}

//---------------------------------------------------------------------------
// connected or disconnected the charging harness
//---------------------------------------------------------------------------
irqreturn_t charging_bat_int(int irq, void *dev_id, struct pt_regs *regs)
    {
    if (!atomic_read(&full_init))
        {
        return IRQ_HANDLED;
        }

    // if we disabled the charging pin, don't handle
    if (!charge)
        {
        return IRQ_HANDLED;
        }

    // We get an interrupt on both edges, so we have to check to which edge
    //  we are responding.
    if (at91_get_gpio_value(INPUT_CHARGING_BAT) == INPUT_CHARGING_BAT_ACTIVE_STATE)
        {
       delay_printk("%s - %s - ACTIVE\n",TARGET_NAME, __func__);
        // we're connected, start the timer and turn off the led
        at91_set_gpio_value(OUTPUT_LED_LOW_BAT, !OUTPUT_LED_LOW_BAT_ACTIVE_STATE);
        mod_timer(&charging_timer_list, jiffies+((LED_CHARGING_OFF_IN_MSECONDS*HZ)/1000));
        }
    else
        {
       delay_printk("%s - %s - INACTIVE\n",TARGET_NAME, __func__);
        // we're disconnected, delete the timer and turn on the led
	del_timer(&charging_timer_list);
        at91_set_gpio_value(OUTPUT_LED_LOW_BAT, OUTPUT_LED_LOW_BAT_ACTIVE_STATE);
        }
    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
// the charging led timer fired
//---------------------------------------------------------------------------
static void reenable_fire(unsigned long data){
   // reenable the battery check
   atomic_set(&check_atomic, 1);
}
//---------------------------------------------------------------------------
// the charging led timer fired
//---------------------------------------------------------------------------
static void charging_fire(unsigned long data)
    {
    // if we disabled the charging pin, don't handle
    if (!charge)
        {
        return;
        }

    // blink on or off?
    if (at91_get_gpio_value(OUTPUT_LED_LOW_BAT) == OUTPUT_LED_LOW_BAT_ACTIVE_STATE)
        {
        // turn off
        at91_set_gpio_value(OUTPUT_LED_LOW_BAT, !OUTPUT_LED_LOW_BAT_ACTIVE_STATE);
        mod_timer(&charging_timer_list, jiffies+((LED_CHARGING_OFF_IN_MSECONDS*HZ)/1000));
        }
    else
        {
        // turn back on, decrement counter
        at91_set_gpio_value(OUTPUT_LED_LOW_BAT, OUTPUT_LED_LOW_BAT_ACTIVE_STATE);
        mod_timer(&charging_timer_list, jiffies+((LED_CHARGING_ON_IN_MSECONDS*HZ)/1000));
        }
     }

//---------------------------------------------------------------------------
// the low battery blink timer fired
//---------------------------------------------------------------------------
static void led_blink_fire(unsigned long data)
    {
    int count, value;

    // are we still low?
    value = atomic_read(&adc_atomic);
    if (atomic_read(&adc_startup) == 0 && value >= BATTERY_NORMAL[minvoltval])
        {
        // we're high, turn led on and return
        at91_set_gpio_value(OUTPUT_LED_LOW_BAT, OUTPUT_LED_LOW_BAT_ACTIVE_STATE);
        return;
        }

    // how many times have we blinked?
    count = atomic_read(&blink_count);

    // blink more?
    if (count > 0)
        {
        // keep blinking...blink on or off?
        if (at91_get_gpio_value(OUTPUT_LED_LOW_BAT) == OUTPUT_LED_LOW_BAT_ACTIVE_STATE)
            {
            // turn off
            at91_set_gpio_value(OUTPUT_LED_LOW_BAT, !OUTPUT_LED_LOW_BAT_ACTIVE_STATE);
            }
        else
            {
            // turn back on, decrement counter
            at91_set_gpio_value(OUTPUT_LED_LOW_BAT, OUTPUT_LED_LOW_BAT_ACTIVE_STATE);
            atomic_set(&blink_count, --count);
            }
        mod_timer(&led_blink_timer_list, jiffies+((LED_BLINK_ON_IN_MSECONDS*HZ)/1000));
        }
    else
        {
        // stop blinking...for now
        atomic_set(&blink_count, LED_BLINK_COUNT);
        at91_set_gpio_value(OUTPUT_LED_LOW_BAT, !OUTPUT_LED_LOW_BAT_ACTIVE_STATE);
        mod_timer(&led_blink_timer_list, jiffies+((LED_BLINK_OFF_IN_MSECONDS*HZ)/1000));
        }
    }

//---------------------------------------------------------------------------
// Set up the ADC to check the battery level
//---------------------------------------------------------------------------
static int hardware_adc_init(void)
    {
	adc_clk = clk_get(NULL, "adc_clk"); 				// Get the ADC Clock
	clk_enable(adc_clk);

	at91_set_A_periph(INPUT_ADC_LOW_BAT, 0); 			// Set the pin to ADC

	adc_base = ioremap(AT91SAM9260_BASE_ADC,SZ_16K); 	// Map the ADC memory region
	__raw_writel(0x01, adc_base + ADC_CR); 				// Reset the ADC

	__raw_writel(ADC_MODE, adc_base + ADC_MR); 			// Mode setup
	__raw_writel(CH_EN, adc_base + ADC_CHER);     		// Enable ADC Channels we are using
	__raw_writel(CH_DIS, adc_base + ADC_CHDR);    		// Disable ADC Channels we are not using

	return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_adc_exit(void)
    {
	clk_disable(adc_clk); 								// Turn off ADC clock
	clk_put(adc_clk);
	iounmap(adc_base);    								// Unmap the ADC memory region

	return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0;
    // correct voltage variable to minimum adc value
    switch (abs(minvoltval))
        {
        case 12: minvoltval = 0;  break; /* 12v is first index */
        case 24: minvoltval = 1; break;  /* 24v is second */
        case 36: minvoltval = 2; break;  /* 36v is third */
        case 48: minvoltval = 3; break;  /* 48v is fourth */
        default: minvoltval = 4; break;  /* error is fifth */
        }
   delay_printk("%s charge: %i, minvoltval: %i\n",__func__,  charge, minvoltval);

    // Enable USB 5V
    at91_set_gpio_input(USB_ENABLE, USB_ENABLE_PULLUP_STATE);

    // Configure charging gpio for input / deglitch
    // if we disabled the charging pin, don't handle
    if (charge)
       {
       at91_set_gpio_input(INPUT_CHARGING_BAT, INPUT_CHARGING_BAT_PULLUP_STATE);
       at91_set_deglitch(INPUT_CHARGING_BAT, INPUT_CHARGING_BAT_DEGLITCH_STATE);
       }

    // configure low battery indicator gpio for output and set initial output
    at91_set_gpio_output(OUTPUT_LED_LOW_BAT, OUTPUT_LED_LOW_BAT_ACTIVE_STATE);

    // blink a couple of times to show we're on (this is the "power on" led after all)
    atomic_set(&blink_count, LED_BLINK_COUNT);
    mod_timer(&led_blink_timer_list, jiffies+((LED_BLINK_ON_IN_MSECONDS*HZ)/1000));

    // check the value for the first time after 10 second
    mod_timer(&timeout_timer_list, jiffies+(10*HZ));

    // configure interrupt
    // if we disabled the charging pin, don't handle
    if (charge)
        {
        status = request_irq(INPUT_CHARGING_BAT, (void*)charging_bat_int, 0, "battery_charging_bat", NULL);
        if (status != 0)
            {
            if (status == -EINVAL)
                {
                   delay_printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", INPUT_CHARGING_BAT);
                }
            else if (status == -EBUSY)
                {
                   delay_printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", INPUT_CHARGING_BAT);
                }

            return status;
            }
        }


    hardware_adc_init();

    return status;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_exit(void)
    {
   del_timer(&reenable_timer_list);
	del_timer(&timeout_timer_list);
	del_timer(&adc_read_timer_list);
	del_timer(&led_blink_timer_list);
	del_timer(&charging_timer_list);
        // if we disabled the charging pin, don't handle
        if (charge)
            {
            free_irq(INPUT_CHARGING_BAT, NULL);
            }
	hardware_adc_exit();
	return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_charging_get(void)
    {
    // if we disabled the charging pin, don't handle
    if (!charge)
        {
        return BATTERY_CHARGING_NO;
        }

	if (at91_get_gpio_value(INPUT_CHARGING_BAT) == INPUT_CHARGING_BAT_ACTIVE_STATE)
	    {
		return BATTERY_CHARGING_YES;
		}
	else
		{
		return BATTERY_CHARGING_NO;
		}
    }

//---------------------------------------------------------------------------
// Handles reads to the charging state attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t charging_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
	return sprintf(buf, "%s\n", battery_charging_state[hardware_charging_get()]);
    }

//---------------------------------------------------------------------------
// Handles reads to the level attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t level_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
	// TODO - return value as percentage?
	unsigned char battery_test;
	battery_test = atomic_read(&adc_atomic); // show last value read
	return sprintf(buf, "%i\n", battery_test);
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(charging, 0444, charging_show, NULL);
static DEVICE_ATTR(level, 0444, level_show, NULL);

//---------------------------------------------------------------------------
// Defines the attributes of the battery for sysfs
//---------------------------------------------------------------------------
static const struct attribute * battery_attrs[] =
    {
    &dev_attr_charging.attr,
    &dev_attr_level.attr,
    NULL,
    };

//---------------------------------------------------------------------------
// Defines the attribute group of the battery for sysfs
//---------------------------------------------------------------------------
const struct attribute_group battery_attr_group =
    {
    .attrs = (struct attribute **) battery_attrs,
    };

//---------------------------------------------------------------------------
// Returns the attribute group of the battery
//---------------------------------------------------------------------------
const struct attribute_group * battery_get_attr_group(void)
    {
    return &battery_attr_group;
    }

//---------------------------------------------------------------------------
// declares the target_device that is added/removed through target.ko
//---------------------------------------------------------------------------
struct target_device target_device_battery =
    {
    .type     		= TARGET_TYPE_BATTERY,
    .name     		= TARGET_NAME,
    .dev     		= NULL,
    .get_attr_group	= battery_get_attr_group,
    };

//---------------------------------------------------------------------------
// Message filler handler for battery values
//---------------------------------------------------------------------------
int bat_mfh(struct sk_buff *skb, void *bat_data) {
    // the bat_data argument is a pre-filled integer
    return nla_put_u8(skb, GEN_INT8_A_MSG, *((int*)bat_data));
}

//---------------------------------------------------------------------------
// Work item to notify the user-space about an adc level change
//---------------------------------------------------------------------------
static void level_changed(struct work_struct * work)
	{
    int adc_val;
    // not initialized or exiting?
    if (atomic_read(&full_init) != TRUE) {
        return;
    }
    
    // notify netlink userspace
	adc_val = atomic_read(&adc_atomic); // grab last value read
    adc_val = bat_from_adc(adc_val); // convert to percentage
    send_nl_message_multi(&adc_val, bat_mfh, NL_C_BATTERY);

    // notifiy sysfs userspace
	target_sysfs_notify(&target_device_battery, "level");
	}

//---------------------------------------------------------------------------
// netlink command handler for battery commands
//---------------------------------------------------------------------------
int nl_battery_handler(struct genl_info *info, struct sk_buff *skb, int cmd, void *ident) {
    struct nlattr *na;
    int rc, value = 0;
//delay_printk("handling battery command\n");
    
    // get attribute from message
    na = info->attrs[GEN_INT8_A_MSG]; // generic 8-bit message
    if (na) {
        // grab value from attribute
        value = nla_get_u8(na);
//delay_printk("received value: %i\n", value);
        switch (value) {
            case BATTERY_SHUTDOWN:
               // shutdown requested, do it soon
               shutdown_soon(false);
               break;
            case BATTERY_REQUEST:
            default:
               // a request is made, continue below
               break;
        }

        // prepare response
        rc = nla_put_u8(skb, GEN_INT8_A_MSG, bat_from_adc(atomic_read(&adc_atomic)));

        // huge success
        if (rc == 0) {
            rc = HANDLE_SUCCESS;
        } else {
           delay_printk("BATTERY: could not create return message\n");
            rc = HANDLE_FAILURE;
        }
    } else {
       delay_printk("BATTERY: could not get attribute\n");
        rc = HANDLE_FAILURE;
    }
//delay_printk("returning rc: %i\n", rc);

    // return to let provider send message back
    return rc;
}

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_battery_init(void)
    {
	int retval, d_id;
    struct driver_command commands[] = {{NL_C_BATTERY, nl_battery_handler}};
    struct nl_driver driver = {NULL, commands, 1, NULL}; // no heartbeat object, 1 command in list, no identifying data structure

delay_printk("%s(): %s - %s\n",__func__,  __DATE__, __TIME__);
	hardware_init();
	retval = target_sysfs_add(&target_device_battery);

        INIT_WORK(&level_work, level_changed);

    // install driver w/ netlink provider
    d_id = install_nl_driver(&driver);
    atomic_set(&driver_id, d_id);

	// signal that we are fully initialized
	atomic_set(&full_init, TRUE);
	return retval;
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_battery_exit(void)
    {
	atomic_set(&full_init, FALSE);
    ati_flush_work(&level_work); // close any open work queue items
    uninstall_nl_driver(atomic_read(&driver_id));
	hardware_exit();
    target_sysfs_remove(&target_device_battery);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_battery_init);
module_exit(target_battery_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target battery module");
MODULE_AUTHOR("jpy");

