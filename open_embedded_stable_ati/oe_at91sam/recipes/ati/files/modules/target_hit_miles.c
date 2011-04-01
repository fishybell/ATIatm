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

#define HIT_SENSOR_DISABLED   			0
#define HIT_SENSOR_ENABLED    			1

#define TESTING_ON_EVAL
#ifdef TESTING_ON_EVAL
	#undef INPUT_MILES_ACTIVE_STATE
	#undef INPUT_MILES_PULLUP_STATE
	#undef INPUT_MILES_DEGLITCH_STATE
	#undef	INPUT_MILES_HIT

	#undef OUTPUT_MILES_RESET_ACTIVE_STATE
	#undef	OUTPUT_MILES_RESET

	#define INPUT_MILES_ACTIVE_STATE			ACTIVE_LOW
	#define INPUT_MILES_PULLUP_STATE			PULLUP_ON
	#define INPUT_MILES_DEGLITCH_STATE			DEGLITCH_ON
	#define	INPUT_MILES_HIT 					AT91_PIN_PA30   // BP3 on dev. board

	#define OUTPUT_MILES_RESET_ACTIVE_STATE		ACTIVE_LOW
	#define	OUTPUT_MILES_RESET 					AT91_PIN_PA6
#endif // TESTING_ON_EVAL

//---------------------------------------------------------------------------
// This atomic variable keeps track of the hit count
//---------------------------------------------------------------------------
atomic_t hit_count_atomic = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that the sensor is enabled.
// No change in settings is permitted when this is true.
//---------------------------------------------------------------------------
atomic_t sensor_enable_atomic = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space of a hit change.
//---------------------------------------------------------------------------
static struct work_struct hit_work;

//---------------------------------------------------------------------------
// Maps the enable to enable name.
//---------------------------------------------------------------------------
static const char * hit_sensor_enable[] =
    {
    "disabled",
    "enabled"
    };

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
irqreturn_t hit_int(int irq, void *dev_id, struct pt_regs *regs)
    {

    // check if we are enabled, if not ignore any input
    if (!atomic_read(&sensor_enable_atomic))
		{
    delay_printk("%s - %s() - disabled, ignoring input.\n",TARGET_NAME, __func__);
        return IRQ_HANDLED;
		}

	// We get an interrupt on both edges, so we have to check to which edge we should respond.
    if (at91_get_gpio_value(INPUT_MILES_HIT) == INPUT_MILES_ACTIVE_STATE)
        {
		atomic_inc(&hit_count_atomic);

	delay_printk("%s - %s() - HIT!\n", TARGET_NAME, __func__);

		// TODO - when do we reset the miles receiver?

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
    at91_set_gpio_output(OUTPUT_MILES_RESET, !OUTPUT_MILES_RESET_ACTIVE_STATE);

    // Configure position gpios for input and deglitch for interrupts
	at91_set_gpio_input(INPUT_MILES_HIT, INPUT_MILES_PULLUP_STATE);
	at91_set_deglitch(INPUT_MILES_HIT, INPUT_MILES_DEGLITCH_STATE);

	status = request_irq(INPUT_MILES_HIT, (void*)hit_int, 0, "hit", NULL);
	if (status != 0)
		{
		if (status == -EINVAL)
			{
		delay_printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", INPUT_MILES_HIT);
			}
		else if (status == -EBUSY)
			{
		delay_printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", INPUT_MILES_HIT);
			}
		return status;
		}

	disable_irq(INPUT_MILES_HIT);

    return status;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_exit(void)
    {
	disable_irq(INPUT_MILES_HIT);
	free_irq(INPUT_MILES_HIT, NULL);
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
	int count;
	if (atomic_read(&sensor_enable_atomic) == HIT_SENSOR_ENABLED)
		{
		count = atomic_read(&hit_count_atomic);
		atomic_sub(count, &hit_count_atomic);
		}
	else
		{
    delay_printk("%s - %s() : hits cannot be read while sensor is disabled.\n",TARGET_NAME, __func__);
		count = 0;
		}
	return sprintf(buf, "%d\n", count);
    }

//---------------------------------------------------------------------------
// Handles reads to the hit sensor enable attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t enable_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", hit_sensor_enable[atomic_read(&sensor_enable_atomic)]);
    }

//---------------------------------------------------------------------------
// Handles writes to the hit sensor enable attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
   if (sysfs_streq(buf, "disable") && (atomic_read(&sensor_enable_atomic) == HIT_SENSOR_ENABLED))
        {
    delay_printk("%s - %s() : hit sensor disabled\n",TARGET_NAME, __func__);

    	atomic_set(&sensor_enable_atomic, HIT_SENSOR_DISABLED);

    	disable_irq(INPUT_MILES_HIT);
        }
    else if (sysfs_streq(buf, "enable") && (atomic_read(&sensor_enable_atomic) == HIT_SENSOR_DISABLED))
        {
    delay_printk("%s - %s() : hit sensor enabled\n",TARGET_NAME, __func__);

    	atomic_set(&hit_count_atomic, 0);

    	enable_irq(INPUT_MILES_HIT);

		// TODO - when do we reset the miles receiver?

    	atomic_set(&sensor_enable_atomic, HIT_SENSOR_ENABLED);
        }

    return size;
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(type, 0444, type_show, NULL);
static DEVICE_ATTR(enable, 0644, enable_show, enable_store);
static DEVICE_ATTR(hit, 0444, hit_show, NULL);


//---------------------------------------------------------------------------
// Defines the attributes of the hit sensor for sysfs
//---------------------------------------------------------------------------
static const struct attribute * hit_sensor_attrs[] =
    {
    &dev_attr_type.attr,
    &dev_attr_enable.attr,
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
delay_printk("%s(): %s - %s\n",__func__,  __DATE__, __TIME__);

	// we are disabled until user-space enables us
	atomic_set(&sensor_enable_atomic, HIT_SENSOR_DISABLED);

	INIT_WORK(&hit_work, hit_change);

	hardware_init();
	
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

