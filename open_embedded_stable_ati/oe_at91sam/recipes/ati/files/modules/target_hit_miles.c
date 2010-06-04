//---------------------------------------------------------------------------
// target_hit_miles.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "target.h"
#include "target_hit_miles.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"hit sensor"
#define SENSOR_TYPE		"miles"

#define PIN_HIT_ACTIVE   			0       		// Active low
#define PIN_HIT    					AT91_PIN_PA30   // BP3 on dev. board

#define PIN_RESET_ACTIVE    		0       		// Active low

#ifdef DEV_BOARD_REVB
	#define PIN_RESET_CONTROL    	AT91_PIN_PA6
#else
	#define PIN_RESET_CONTROL    	AT91_PIN_PB8
#endif

// TODO - make sure to protect local variables
// maybe make then atomic?, but we don't want changes to while we are processing

//---------------------------------------------------------------------------
// Keep track of the hit count
//---------------------------------------------------------------------------
static int hit_count = 0;

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space of a hit change.
//---------------------------------------------------------------------------
static struct work_struct hit_work;

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
irqreturn_t hit_int(int irq, void *dev_id, struct pt_regs *regs)
    {
	// We get an interrupt on both edges, so we have to check to which edge
	// we are responding.
    if (at91_get_gpio_value(PIN_HIT) == PIN_HIT_ACTIVE)
        {
    	printk(KERN_ALERT "%s - %s()\n",TARGET_NAME, __func__);

    	hit_count++;

    	// TODO - reset miles?

        // signal that the operation has finished
    	// atomic_set(&operating_atomic, 0);

        // notify user-space
        schedule_work(&hit_work);
        }

    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0;

    // configure reset gpio for output and set initial output
    at91_set_gpio_output(PIN_RESET_CONTROL, !PIN_RESET_ACTIVE);

    // Configure position gpios for input and deglitch for interrupts
	at91_set_gpio_input(PIN_HIT, 1);
	at91_set_deglitch(PIN_HIT, 1);

	status = request_irq(PIN_HIT, (void*)hit_int, 0, "hit", NULL);
	if (status != 0)
		{
		if (status == -EINVAL)
			{
			printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", PIN_HIT);
			}
		else if (status == -EBUSY)
			{
			printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", PIN_HIT);
			}
		return status;
		}

    return status;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_exit(void)
    {
	free_irq(PIN_HIT, NULL);
	return 0;
    }

//---------------------------------------------------------------------------
// Handles reads to the type attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t type_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", SENSOR_TYPE);
    }

//---------------------------------------------------------------------------
// Handles reads to the hit attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t hit_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
	// TODO - synch with the algorithm
	int count = hit_count;
	hit_count = 0;
    return sprintf(buf, "%d\n",count);
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(type, 0444, type_show, NULL);
static DEVICE_ATTR(hit, 0444, hit_show, NULL);


//---------------------------------------------------------------------------
// Defines the attributes of the hit sensor for sysfs
//---------------------------------------------------------------------------
static const struct attribute * hit_sensor_attrs[] =
    {
    &dev_attr_type.attr,
    &dev_attr_hit.attr,
    NULL,
    };

//---------------------------------------------------------------------------
// Defines the attribute group of the hit sensor for sysfs
//---------------------------------------------------------------------------
const struct attribute_group hit_sensor_attr_group =
    {
    .attrs = (struct attribute **) hit_sensor_attrs,
    };

//---------------------------------------------------------------------------
// Returns the attribute group of the hit sensor
//---------------------------------------------------------------------------
const struct attribute_group * hit_sensor_get_attr_group(void)
    {
    return &hit_sensor_attr_group;
    }

//---------------------------------------------------------------------------
// declares the target_device that is added/removed through target.ko
//---------------------------------------------------------------------------
struct target_device target_device_hit_miles =
    {
    .type     		= TARGET_TYPE_HIT_SENSOR,
    .name     		= TARGET_NAME,
    .dev     		= NULL,
    .get_attr_group	= hit_sensor_get_attr_group,
    };

//---------------------------------------------------------------------------
// Work item to notify the user-space about a hit change
//---------------------------------------------------------------------------
static void hit_change(struct work_struct * work)
	{
	target_sysfs_notify(&target_device_hit_miles, "hit");
	}

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_hit_miles_init(void)
    {
	printk(KERN_ALERT "%s(): %s - %s\n",__func__,  __DATE__, __TIME__);
	hardware_init();
	INIT_WORK(&hit_work, hit_change);
    return target_sysfs_add(&target_device_hit_miles);
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_hit_miles_exit(void)
    {
	hardware_exit();
    target_sysfs_remove(&target_device_hit_miles);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_hit_miles_init);
module_exit(target_hit_miles_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target hit sensor (miles) module");
MODULE_AUTHOR("jpy");

