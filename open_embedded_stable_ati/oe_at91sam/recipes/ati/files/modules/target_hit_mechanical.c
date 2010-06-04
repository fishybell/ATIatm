//---------------------------------------------------------------------------
// target_hit_mechanical.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "target.h"
#include "target_hit_mechanical.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"hit sensor"
#define SENSOR_TYPE		"mechanical"

#define TIMEOUT_IN_SECONDS	2

#define HIT_SENSOR_MODE_SINGLE   	0
#define HIT_SENSOR_MODE_BURST    	1


// TODO - make sure to protect local variables
// maybe make then atomic?, but we don't want changes to while we are processing


//---------------------------------------------------------------------------
// Keep track of the mode /  burst separation setting
//---------------------------------------------------------------------------
static int sensor_mode 				= HIT_SENSOR_MODE_SINGLE;
static int sensor_sensitivity 		= 1; 	// 1 - 15
static int sensor_burst_separation 	= 250;	// 100 - 10000 milliseconds

static int hit_count 				= 0;

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that an operation is in progress,
// i.e. the input is being processed.
//---------------------------------------------------------------------------
atomic_t operating_atomic = ATOMIC_INIT(0);

//---------------------------------------------------------------------------
// This delayed work queue item is used to notify user-space of a hit change.
//---------------------------------------------------------------------------
static struct work_struct hit_work;

//---------------------------------------------------------------------------
// Declaration of the function that gets called when the timeout fires.
//---------------------------------------------------------------------------
static void timeout_fire(unsigned long data);

//---------------------------------------------------------------------------
// Kernel timer for the timeout.
//---------------------------------------------------------------------------
static struct timer_list timeout_timer_list = TIMER_INITIALIZER(timeout_fire, 0, 0);

//---------------------------------------------------------------------------
// Maps the mode to mode name.
//---------------------------------------------------------------------------
static const char * hit_sensor_mode[] =
    {
    "single",
    "burst"
    };

//---------------------------------------------------------------------------
// Starts the timeout timer.
//---------------------------------------------------------------------------
static void timeout_timer_start(void)
	{
	// TODO - add single vs. burst setting
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
	// TODO - hit de-bounce algorithm

    // signal that the operation has finished
    atomic_set(&operating_atomic, 0);
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
irqreturn_t hit_int(int irq, void *dev_id, struct pt_regs *regs)
    {
	// We get an interrupt on both edges, so we have to check to which edge
	// we are responding.
    if (at91_get_gpio_value(INPUT_HIT_SENSOR) == INPUT_HIT_SENSOR_ACTIVE_STATE)
        {
    	// TODO - add algorithm
//    	printk(KERN_ALERT "%s - %s()\n",TARGET_NAME, __func__);

    	hit_count++;

        // signal that the operation has finished
//    	atomic_set(&operating_atomic, 0);

        // notify user-space
//        schedule_work(&hit_work);
        }

    return IRQ_HANDLED;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0;

    // Configure position gpios for input and deglitch for interrupts
	at91_set_gpio_input(INPUT_HIT_SENSOR, INPUT_HIT_SENSOR_PULLUP_STATE);
	at91_set_deglitch(INPUT_HIT_SENSOR, INPUT_HIT_SENSOR_DEGLITCH_STATE);

	status = request_irq(INPUT_HIT_SENSOR, (void*)hit_int, 0, "hit", NULL);
	if (status != 0)
		{
		if (status == -EINVAL)
			{
			printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", INPUT_HIT_SENSOR);
			}
		else if (status == -EBUSY)
			{
			printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", INPUT_HIT_SENSOR);
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
	free_irq(INPUT_HIT_SENSOR, NULL);
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
// Handles reads to the hit sensor mode attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
    return sprintf(buf, "%s\n", hit_sensor_mode[sensor_mode]);
    }

//---------------------------------------------------------------------------
// Handles writes to the hit sensor mode attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    ssize_t status;

    // check if an operation is in progress, if so ignore any command
    if (atomic_read(&operating_atomic))
		{
    	status = size;
		}
    else if (sysfs_streq(buf, "single"))
        {
    	printk(KERN_ALERT "%s - %s() : mode set to single\n",TARGET_NAME, __func__);
    	sensor_mode = HIT_SENSOR_MODE_SINGLE;
    	status = size;
        }
    else if (sysfs_streq(buf, "burst"))
        {
    	printk(KERN_ALERT "%s - %s() : mode set to burst\n",TARGET_NAME, __func__);
    	sensor_mode = HIT_SENSOR_MODE_BURST;
    	status = size;
        }
    else
		{
    	status = size;
		}

    return status;
    }

//---------------------------------------------------------------------------
// Handles reads to the hit sensor sensitivity attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t sensitivity_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
	return sprintf(buf, "%d\n", sensor_sensitivity);
    }

//---------------------------------------------------------------------------
// Handles writes to the hit sensor sensitivity attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t sensitivity_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    long value;
    ssize_t status;

    // check if an operation is in progress, if so ignore any command
    if (atomic_read(&operating_atomic))
		{
    	status = size;
		}
    else if ((strict_strtol(buf, 0, &value) == 0) &&
			(value >= 1) &&
			(value <= 15))
		{
		sensor_sensitivity = value;
		status = size;
		}
	else
		{
		printk(KERN_ALERT "%s - %s() : sensitivity out of range 1-15 (%s)\n",TARGET_NAME, __func__, buf);
		status = size;
		}

    return status;
    }

//---------------------------------------------------------------------------
// Handles reads to the hit sensor burst separation attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t burst_separation_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
	return sprintf(buf, "%d\n", sensor_burst_separation);
    }

//---------------------------------------------------------------------------
// Handles writes to the burst separation attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t burst_separation_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    long value;
    ssize_t status;

    // check if an operation is in progress, if so ignore any command
    if (atomic_read(&operating_atomic))
		{
    	status = size;
		}
    else if ((strict_strtol(buf, 0, &value) == 0) &&
			(value >= 100) &&
			(value <= 10000))
		{
		sensor_burst_separation = value;
		status = size;
		}
	else
		{
		printk(KERN_ALERT "%s - %s() : burst separation out of range 100-10000 (%s)\n",TARGET_NAME, __func__, buf);
		status = size;
		}

    return status;
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(type, 0444, type_show, NULL);
static DEVICE_ATTR(hit, 0444, hit_show, NULL);
static DEVICE_ATTR(mode, 0644, mode_show, mode_store);
static DEVICE_ATTR(sensitivity, 0644, sensitivity_show, sensitivity_store);
static DEVICE_ATTR(burst_separation, 0644, burst_separation_show, burst_separation_store);

//---------------------------------------------------------------------------
// Defines the attributes of the hit sensor for sysfs
//---------------------------------------------------------------------------
static const struct attribute * hit_sensor_attrs[] =
    {
    &dev_attr_type.attr,
    &dev_attr_hit.attr,
    &dev_attr_mode.attr,
    &dev_attr_sensitivity.attr,
    &dev_attr_burst_separation.attr,
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
struct target_device target_device_hit_mechanical =
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
	target_sysfs_notify(&target_device_hit_mechanical, "hit");
	}

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_hit_mechanical_init(void)
    {
	printk(KERN_ALERT "%s(): %s - %s\n",__func__,  __DATE__, __TIME__);
	hardware_init();
	INIT_WORK(&hit_work, hit_change);
    return target_sysfs_add(&target_device_hit_mechanical);
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_hit_mechanical_exit(void)
    {
	hardware_exit();
    target_sysfs_remove(&target_device_hit_mechanical);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_hit_mechanical_init);
module_exit(target_hit_mechanical_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target hit sensor (mechanical) module");
MODULE_AUTHOR("jpy");

