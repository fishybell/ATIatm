//---------------------------------------------------------------------------
// target_sound.c
//---------------------------------------------------------------------------
#include <linux/device.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>

#include "target.h"
#include "target_sound.h"
//---------------------------------------------------------------------------

#define TARGET_NAME		"sound amp"

// TODO - hw interface to amplifier


//---------------------------------------------------------------------------
// Keep track of the volume setting
//---------------------------------------------------------------------------
static int sound_amp_volume 	= 20;

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_volume_set(int volume)
	{
	// TODO - set volume
	return 0;
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_volume_get(void)
	{
	return sound_amp_volume;
	}

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_init(void)
    {
    int status = 0;

    // TODO - set up hw interface to volume level

    return status;
    }

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static int hardware_exit(void)
    {
	return 0;
    }

//---------------------------------------------------------------------------
// Handles reads to the sound volume level attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t volume_show(struct device *dev, struct device_attribute *attr, char *buf)
    {
	return sprintf(buf, "%d\n", sound_amp_volume);
    }

//---------------------------------------------------------------------------
// Handles writes to the sound volume level attribute through sysfs
//---------------------------------------------------------------------------
static ssize_t volume_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
    {
    long value;
    ssize_t status;

    if ((strict_strtol(buf, 0, &value) == 0) &&
			(value >= 0) &&
			(value <= 100))
		{
		sound_amp_volume = value;
		status = size;
		}
	else
		{
		printk(KERN_ALERT "%s - %s() : sound volume of range 0-100 (%s)\n",TARGET_NAME, __func__, buf);
		status = size;
		}

    return status;
    }

//---------------------------------------------------------------------------
// maps attributes, r/w permissions, and handler functions
//---------------------------------------------------------------------------
static DEVICE_ATTR(volume, 0644, volume_show, volume_store);

//---------------------------------------------------------------------------
// Defines the attributes of the sound amp for sysfs
//---------------------------------------------------------------------------
static const struct attribute * sound_attrs[] =
    {
    &dev_attr_volume.attr,
    NULL,
    };

//---------------------------------------------------------------------------
// Defines the attribute group of the sound amp for sysfs
//---------------------------------------------------------------------------
const struct attribute_group sound_attr_group =
    {
    .attrs = (struct attribute **) sound_attrs,
    };

//---------------------------------------------------------------------------
// Returns the attribute group of the sound amp
//---------------------------------------------------------------------------
const struct attribute_group * sound_get_attr_group(void)
    {
    return &sound_attr_group;
    }

//---------------------------------------------------------------------------
// declares the target_device that is added/removed through target.ko
//---------------------------------------------------------------------------
struct target_device target_device_sound =
    {
    .type     		= TARGET_TYPE_SOUND,
    .name     		= TARGET_NAME,
    .dev     		= NULL,
    .get_attr_group	= sound_get_attr_group,
    };

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_sound_init(void)
    {
	printk(KERN_ALERT "%s(): %s - %s\n",__func__,  __DATE__, __TIME__);
	hardware_init();
    return target_sysfs_add(&target_device_sound);
    }

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_sound_exit(void)
    {
	hardware_exit();
    target_sysfs_remove(&target_device_sound);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_sound_init);
module_exit(target_sound_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI target sound amp module");
MODULE_AUTHOR("jpy");

