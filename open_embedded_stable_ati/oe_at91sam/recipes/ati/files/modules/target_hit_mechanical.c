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

// Convert milliseconds to nanoseconds needed
// for hi-res timers
#define MS_TO_NS(x)	((int)x * (int)1E6)

#define HIT_SENSOR_DISABLED   			0
#define HIT_SENSOR_ENABLED    			1

#define HIT_SENSOR_MODE_SINGLE   		0
#define HIT_SENSOR_MODE_BURST    		1

#define SAMPLE_COUNT_MAX_DEFAULT 		50
#define SAMPLE_PERIOD_IN_MS_DEFAULT 	2

//---------------------------------------------------------------------------
// Keep track settings
//---------------------------------------------------------------------------
static int sensor_mode 				= HIT_SENSOR_MODE_SINGLE;
static int sensor_sensitivity 		= 1; 	// 1 - 15
static int sensor_burst_separation 	= 250;	// 100 - 10000 milliseconds

static int setting_sample_count_total 	= SAMPLE_COUNT_MAX_DEFAULT;
static int setting_sample_period_ms 	= SAMPLE_PERIOD_IN_MS_DEFAULT;

//---------------------------------------------------------------------------
// Module variables
//---------------------------------------------------------------------------
static int sample_current			= 0;
static int sample_count_active		= 0;
static int sample_count_total		= 0;


//#define ktime_sample_period ktime_set_time(setting_sample_period_ms/1000, MS_TO_NS(setting_sample_period_ms%1000))
static ktime_t ktime_sample_period;
static ktime_t ktime_burst_separation;

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
// Kernel hi res timer for taking periodic samples of the hit sensor
//---------------------------------------------------------------------------
static struct hrtimer hi_res_timer_sampling;

//---------------------------------------------------------------------------
// Kernel hi res timer for enforcing burst separation
//---------------------------------------------------------------------------
static struct hrtimer hi_res_timer_burst_separation;

//---------------------------------------------------------------------------
// Maps the enable to enable name.
//---------------------------------------------------------------------------
static const char * hit_sensor_enable[] =
    {
    "disabled",
    "enabled"
    };

//---------------------------------------------------------------------------
// Maps the mode to mode name.
//---------------------------------------------------------------------------
static const char * hit_sensor_mode[] =
    {
    "single",
    "burst"
    };

//---------------------------------------------------------------------------
// Helper function for ktime_t
//---------------------------------------------------------------------------
static inline void ktime_set_time(ktime_t * time, int secs, int nsecs)
	{
	time->tv.sec = (s32) secs;
	time->tv.nsec = (s32) nsecs;
	}

//---------------------------------------------------------------------------
// Gets called when the hi res burst separation timer fires.
//---------------------------------------------------------------------------
static enum hrtimer_restart hr_timer_enable_irq(struct hrtimer *timer)
	{
	enable_irq(INPUT_HIT_SENSOR);
	return HRTIMER_NORESTART;
	}

//---------------------------------------------------------------------------
// Gets called when the hi res sample timer fires.
//---------------------------------------------------------------------------
static enum hrtimer_restart hr_timer_get_sample(struct hrtimer *timer)
	{
	sample_current = at91_get_gpio_value(INPUT_HIT_SENSOR);

	// ...otherwise, decrement the high or low counter depending on the input
	if (sample_current == INPUT_HIT_SENSOR_ACTIVE_STATE)
		{
		sample_count_active++;
		}

	sample_count_total--;

	if (sample_count_total == 0)
		{
	delay_printk("%02i:%i\n",  sample_count_active, setting_sample_count_total);

		// TODO - is it a hit? check the sensitivity setting - currently a hack, maybe
		// have a look-up table...
		if(sample_count_active >= sensor_sensitivity)
			{
			atomic_inc(&hit_count_atomic);

		delay_printk("%s - %s() - HIT!\n", TARGET_NAME, __func__);

	        // notify user-space
			schedule_work(&hit_work);
			}

		if (sensor_mode == HIT_SENSOR_MODE_SINGLE)
			{
			enable_irq(INPUT_HIT_SENSOR);
			return HRTIMER_NORESTART;
			}
		else
			{
			target_hrtimer_start(&hi_res_timer_burst_separation, ktime_burst_separation, HRTIMER_MODE_REL);
			return HRTIMER_NORESTART;
			}
		}
	else
		{
		target_hrtimer_forward_now(&hi_res_timer_sampling, ktime_set(SAMPLE_PERIOD_IN_MS_DEFAULT/1000, MS_TO_NS(SAMPLE_PERIOD_IN_MS_DEFAULT%1000)));
		return HRTIMER_RESTART;
		}
	}

//---------------------------------------------------------------------------
// Stops the hi res timers.
//---------------------------------------------------------------------------
static inline void hr_timers_stop(void)
	{
	target_hrtimer_cancel(&hi_res_timer_burst_separation);
	target_hrtimer_cancel(&hi_res_timer_sampling);
	}

//---------------------------------------------------------------------------
// The function initializes the "debouncer"
//---------------------------------------------------------------------------
static void filter_init(void)
	{
	sample_count_active		= 0;
	sample_count_total		= setting_sample_count_total;
	}

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
    if (at91_get_gpio_value(INPUT_HIT_SENSOR) == INPUT_HIT_SENSOR_ACTIVE_STATE)
        {
    delay_printk("%s - %s()\n",TARGET_NAME, __func__);

    	// Disable the interrupt (ourselves)
    	disable_irq_nosync(INPUT_HIT_SENSOR);

    	filter_init();

    	// Start taking periodic samples
    	target_hrtimer_start(&hi_res_timer_sampling, ktime_sample_period, HRTIMER_MODE_REL);
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
		delay_printk(KERN_ERR "request_irq() failed - invalid irq number (%d) or handler\n", INPUT_HIT_SENSOR);
			}
		else if (status == -EBUSY)
			{
		delay_printk(KERN_ERR "request_irq(): irq number (%d) is busy, change your config\n", INPUT_HIT_SENSOR);
			}
		return status;
		}

	disable_irq(INPUT_HIT_SENSOR);

    return status;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_exit(void)
    {
	disable_irq(INPUT_HIT_SENSOR);
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

    	hr_timers_stop();

    	disable_irq(INPUT_HIT_SENSOR);
        }
    else if (sysfs_streq(buf, "enable") && (atomic_read(&sensor_enable_atomic) == HIT_SENSOR_DISABLED))
        {
    delay_printk("%s - %s() : hit sensor enabled\n",TARGET_NAME, __func__);

    	atomic_set(&hit_count_atomic, 0);

    	ktime_set_time(&ktime_burst_separation, sensor_burst_separation/1000, MS_TO_NS(sensor_burst_separation%1000));
    	ktime_set_time(&ktime_sample_period, setting_sample_period_ms/1000, MS_TO_NS(setting_sample_period_ms%1000));

    	enable_irq(INPUT_HIT_SENSOR);

    	atomic_set(&sensor_enable_atomic, HIT_SENSOR_ENABLED);
        }

    return size;
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
    // check if we are enabled, if so ignore any changes to settings
    if (atomic_read(&sensor_enable_atomic) == HIT_SENSOR_ENABLED)
		{
    delay_printk("%s - %s() : no changes can be made to settings while the sensor is enabled.\n",TARGET_NAME, __func__);
		}
    else if (sysfs_streq(buf, "single"))
        {
    delay_printk("%s - %s() : mode set to single\n",TARGET_NAME, __func__);
    	sensor_mode = HIT_SENSOR_MODE_SINGLE;
        }
    else if (sysfs_streq(buf, "burst"))
        {
    delay_printk("%s - %s() : mode set to burst\n",TARGET_NAME, __func__);
    	sensor_mode = HIT_SENSOR_MODE_BURST;
        }

    return size;
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

    // check if we are enabled, if so ignore any changes to settings
    if (atomic_read(&sensor_enable_atomic) == HIT_SENSOR_ENABLED)
		{
    delay_printk("%s - %s() : no changes can be made to settings while the sensor is enabled.\n",TARGET_NAME, __func__);
		}
	else
		{
		if ((strict_strtol(buf, 0, &value) == 0) &&
			(value >= 1) &&
			(value <= 15))
			{
			sensor_sensitivity = value;
			}
		else
			{
		delay_printk("%s - %s() : sensitivity out of range 1-15 (%s)\n",TARGET_NAME, __func__, buf);
			}
		}
    return size;
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

    // check if we are enabled, if so ignore any changes to settings
    if (atomic_read(&sensor_enable_atomic) == HIT_SENSOR_ENABLED)
		{
    delay_printk("%s - %s() : no changes can be made to settings while the sensor is enabled.\n",TARGET_NAME, __func__);
		}
    else
		{
    	if ((strict_strtol(buf, 0, &value) == 0) &&
			(value >= 100) &&
			(value <= 10000))
			{
			sensor_burst_separation = value;
			}
		else
			{
		delay_printk("%s - %s() : burst separation out of range 100-10000 (%s)\n",TARGET_NAME, __func__, buf);
			}
		}
    return size;
    }

//---------------------------------------------------------------------------
// Handles reads to the hit sensor sample count attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t sample_count_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
	return sprintf(buf, "%d\n", setting_sample_count_total);
    }

//---------------------------------------------------------------------------
// Handles writes to the sample count attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t sample_count_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    long value;

    // check if we are enabled, if so ignore any changes to settings
    if (atomic_read(&sensor_enable_atomic) == HIT_SENSOR_ENABLED)
		{
    delay_printk("%s - %s() : no changes can be made to settings while the sensor is enabled.\n",TARGET_NAME, __func__);
		}
    else
		{
		if ((strict_strtol(buf, 0, &value) == 0) &&
				(value >= 100) &&
				(value <= 1000))
			{
			setting_sample_count_total = value;
			}
		else
			{
		delay_printk("%s - %s() : sample count out of range 100-1000 (%s)\n",TARGET_NAME, __func__, buf);
			}
		}

    return size;
    }

//---------------------------------------------------------------------------
// Handles reads to the hit sensor sample period attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t sample_period_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
	return sprintf(buf, "%d\n", setting_sample_period_ms);
    }

//---------------------------------------------------------------------------
// Handles writes to the sample count attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t sample_period_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    long value;

    // check if we are enabled, if so ignore any changes to settings
    if (atomic_read(&sensor_enable_atomic) == HIT_SENSOR_ENABLED)
		{
    delay_printk("%s - %s() : no changes can be made to settings while the sensor is enabled.\n",TARGET_NAME, __func__);
		}
    else
		{
		if ((strict_strtol(buf, 0, &value) == 0) &&
				(value >= 1) &&
				(value <= 10))
			{
			setting_sample_period_ms = value;
			}
		else
			{
		delay_printk("%s - %s() : sample period out of range 1-10 (%s)\n",TARGET_NAME, __func__, buf);
			}
		}

    return size;
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(type, 0444, type_show, NULL);
static DEVICE_ATTR(enable, 0644, enable_show, enable_store);
static DEVICE_ATTR(sample_count, 0644, sample_count_show, sample_count_store);
static DEVICE_ATTR(sample_period, 0644, sample_period_show, sample_period_store);
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
    &dev_attr_enable.attr,
    &dev_attr_sample_count.attr,
    &dev_attr_sample_period.attr,
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
delay_printk("%s(): %s - %s\n",__func__,  __DATE__, __TIME__);

	// we are disabled until user-space enables us
	atomic_set(&sensor_enable_atomic, HIT_SENSOR_DISABLED);

	INIT_WORK(&hit_work, hit_change);

	target_hrtimer_init(&hi_res_timer_sampling, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hi_res_timer_sampling.function = hr_timer_get_sample;

	target_hrtimer_init(&hi_res_timer_burst_separation, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hi_res_timer_burst_separation.function = hr_timer_enable_irq;

	hardware_init();

    return target_sysfs_add(&target_device_hit_mechanical);
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_hit_mechanical_exit(void)
    {
	hr_timers_stop();
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

