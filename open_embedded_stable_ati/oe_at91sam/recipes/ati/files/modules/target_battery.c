//---------------------------------------------------------------------------
// target_battery.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <mach/gpio.h>

#include "target.h"
#include "target_battery.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"battery"

// TODO - the ADC code could be broken into its own file to allows configuration
// and use of more than one ADC channel at a time.

// TODO - make this timeout longer, we don't need to check the battery every 5 seconds
#define TIMEOUT_IN_SECONDS	5

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

	#define	INPUT_ADC_LOW_BAT 						AT91_PIN_PC1//PC0

	#define OUTPUT_LED_LOW_BAT_ACTIVE_STATE			ACTIVE_LOW
	#define	OUTPUT_LED_LOW_BAT 						AT91_PIN_PC15 //PA6 LED on dev. board

	#define INPUT_CHARGING_BAT_ACTIVE_STATE			ACTIVE_LOW
	#define INPUT_CHARGING_BAT_PULLUP_STATE			PULLUP_OFF
	#define INPUT_CHARGING_BAT_DEGLITCH_STATE		DEGLITCH_ON
	#define	INPUT_CHARGING_BAT					AT91_PIN_PB8 // PB3 on dev. board
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
// Declaration of the function that gets called when the timeout fires.
//---------------------------------------------------------------------------
static void timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the timeout.
//---------------------------------------------------------------------------
static struct timer_list timeout_timer_list = TIMER_INITIALIZER(timeout_fire, 0, 0);

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
//
//---------------------------------------------------------------------------
static unsigned char hardware_adc_read(void)
	{
	__raw_writel(ADC_START, adc_base + ADC_CR); // Start the ADC
	while(!(__raw_readl(adc_base + ADC_SR) & CH_EN))
		{
		cpu_relax();
		}
	//return __raw_readl(adc_base + ADC_CDR0); // Read & Return the conversion
	return __raw_readl(adc_base + ADC_CDR); // Read & Return the conversion
	}

//---------------------------------------------------------------------------
// Starts the timeout timer.
//---------------------------------------------------------------------------
static void timeout_timer_start(void)
	{
	mod_timer(&timeout_timer_list, jiffies+(TIMEOUT_IN_SECONDS*HZ));
	}

//---------------------------------------------------------------------------
// Stops the timeout timer.
//---------------------------------------------------------------------------
static void timeout_timer_stop(void)
	{
	del_timer(&timeout_timer_list);
	}

//---------------------------------------------------------------------------
// The function that gets called when the timeout fires.
//---------------------------------------------------------------------------
static void timeout_fire(unsigned long data)
	{
	char battery_test;

	battery_test = hardware_adc_read();

	printk(KERN_ALERT "ADC = %i\n", battery_test);

	// TODO  determine if battery is low, based on ADC value
	// now we just toggle the LED
	if(at91_get_gpio_value(OUTPUT_LED_LOW_BAT) == OUTPUT_LED_LOW_BAT_ACTIVE_STATE)
		{
		at91_set_gpio_value(OUTPUT_LED_LOW_BAT, !OUTPUT_LED_LOW_BAT_ACTIVE_STATE);
		}
	else
		{
		at91_set_gpio_value(OUTPUT_LED_LOW_BAT, OUTPUT_LED_LOW_BAT_ACTIVE_STATE);
		}



	// Restart the timer
	mod_timer(&timeout_timer_list, jiffies+(TIMEOUT_IN_SECONDS*HZ));
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
printk("adc_base: %08x\n",(int)adc_base);
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

    // Configure charging gpio for input / deglitch
    at91_set_gpio_input(INPUT_CHARGING_BAT, 1);
    at91_set_deglitch(INPUT_CHARGING_BAT, 1);

    // configure low battery indicator gpio for output and set initial output
    at91_set_gpio_output(OUTPUT_LED_LOW_BAT, !OUTPUT_LED_LOW_BAT_ACTIVE_STATE);

    hardware_adc_init();

    timeout_timer_start();

    return status;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_exit(void)
    {
	timeout_timer_stop();
	hardware_adc_exit();
	return 0;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_charging_get(void)
    {
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
	char battery_test;
	battery_test = hardware_adc_read();
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
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_battery_init(void)
    {
	printk(KERN_ALERT "%s(): %s - %s\n",__func__,  __DATE__, __TIME__);
	hardware_init();
    return target_sysfs_add(&target_device_battery);
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_battery_exit(void)
    {
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

