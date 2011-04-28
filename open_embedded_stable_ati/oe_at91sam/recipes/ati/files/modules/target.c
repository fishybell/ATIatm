//---------------------------------------------------------------------------
// target.c
//---------------------------------------------------------------------------
#include <linux/init.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/atmel_tc.h>
#include <linux/hrtimer.h>

#include "target.h"
#include "delay_printk.h"
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// This lock protects against target_sysfs_add/remove being called
// simultaneously.
//---------------------------------------------------------------------------
static DEFINE_MUTEX(sysfs_lock);

//---------------------------------------------------------------------------
// maps types to type names
//---------------------------------------------------------------------------
static const char * target_type[] =
    {
    "none",
    "lifter",
    "mover",
    "hit_sensor",
    "muzzle_flash",
    "miles_transmitter",
    "sound",
    "thermal",
    "battery",
    "user_interface",
    "ses_interface",
    "mover_position",
    "scott_test"
    };

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static struct class_attribute target_class_attrs[] =
    {
    __ATTR_NULL
    };

//---------------------------------------------------------------------------
// Passthrough for atmel_tc_alloc
//---------------------------------------------------------------------------
struct atmel_tc * target_timer_alloc(unsigned block, const char *name)
	{
	return atmel_tc_alloc(block, name);
	}
EXPORT_SYMBOL(target_timer_alloc);

//---------------------------------------------------------------------------
// Passthrough for atmel_tc_free
//---------------------------------------------------------------------------
void target_timer_free(struct atmel_tc *tc)
	{
	atmel_tc_free(tc);
	}
EXPORT_SYMBOL(target_timer_free);

//---------------------------------------------------------------------------
// Passthrough for hrtimer_init
//---------------------------------------------------------------------------
void target_hrtimer_init(struct hrtimer *timer, clockid_t which_clock, enum hrtimer_mode mode)
	{
	hrtimer_init(timer, which_clock,  mode);
	}
EXPORT_SYMBOL(target_hrtimer_init);

//---------------------------------------------------------------------------
// Passthrough for hrtimer_start
//---------------------------------------------------------------------------
extern int target_hrtimer_start(struct hrtimer *timer, ktime_t tim, const enum hrtimer_mode mode)
	{
	return hrtimer_start(timer, tim,  mode);
	}
EXPORT_SYMBOL(target_hrtimer_start);

//---------------------------------------------------------------------------
// Passthrough for hrtimer_forward_now
//---------------------------------------------------------------------------
u64 target_hrtimer_forward_now(struct hrtimer *timer, ktime_t interval)
	{
	return hrtimer_forward(timer, timer->base->get_time(), interval);
	}
EXPORT_SYMBOL(target_hrtimer_forward_now);

//---------------------------------------------------------------------------
// Passthrough for hrtimer_cancel
//---------------------------------------------------------------------------
int target_hrtimer_cancel(struct hrtimer *timer)
	{
	return hrtimer_cancel(timer);
	}
EXPORT_SYMBOL(target_hrtimer_cancel);

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
static struct class target_class =
	{
	.name =  "target",
	.owner = THIS_MODULE,

	.class_attrs = target_class_attrs,
	};

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
int target_sysfs_add(struct target_device * target_device)
    {
    int status = 0;

    mutex_lock(&sysfs_lock);

    // TODO - add number scheme for multiple targets of the same type
    // we'll just have one of each type for now...
    // target_device->dev = device_create(&target_class, NULL, MKDEV(0, 0), target_device->name, "%s%d",target_type[target_device->type], 0);
    target_device->dev = device_create(&target_class, NULL, MKDEV(0, 0), target_device->name, "%s",target_type[target_device->type]);
    if (target_device->dev)
        {
        const struct attribute_group * attr_group = target_device->get_attr_group();
       delay_printk("device_create() succeeded.\n");
        status = sysfs_create_group(&(target_device->dev->kobj), attr_group);
        if (status != 0)
            {
           delay_printk("sysfs_create_group() failed.\n");
            device_unregister(target_device->dev);
            }
        else
            {
           delay_printk("sysfs_create_group() succeeded.\n");
            }
        }
    else
        {
       delay_printk("device_create() failed.\n");
        status = -ENODEV;
        }

    mutex_unlock(&sysfs_lock);

    return status;
    }
EXPORT_SYMBOL(target_sysfs_add);

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
void target_sysfs_remove(struct target_device * target_device)
    {
	mutex_lock(&sysfs_lock);

    device_unregister(target_device->dev);

    mutex_unlock(&sysfs_lock);
    }
EXPORT_SYMBOL(target_sysfs_remove);

//---------------------------------------------------------------------------
//
//---------------------------------------------------------------------------
void target_sysfs_notify(struct target_device * target_device, char * attribute_name)
    {
    sysfs_notify(&target_device->dev->kobj, NULL, attribute_name);
    }
EXPORT_SYMBOL(target_sysfs_notify);

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init target_init(void)
    {
    int status = 0;

   delay_printk("%s(): %s - %s\n",__func__,  __DATE__, __TIME__);

    mutex_lock(&sysfs_lock);

    status = class_register(&target_class);
    if (status < 0)
        {
       delay_printk("class_register() failed.\n");
        }

    mutex_unlock(&sysfs_lock);

    return status;
    }


//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit target_exit(void)
    {
   delay_printk("%s()\n", __func__);

    // this is here only while debugging. We would not want to remove the class
    // while other drivers may still need it or while user space has a handle open.
    // But this allows us to load/unload the module without rebooting.
    mutex_lock(&sysfs_lock);
    class_unregister(&target_class);
    mutex_unlock(&sysfs_lock);
    }


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(target_init);
module_exit(target_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ATI target module");
MODULE_AUTHOR("jpy");
