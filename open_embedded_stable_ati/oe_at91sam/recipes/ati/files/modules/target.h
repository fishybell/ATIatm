//---------------------------------------------------------------------------
// target.h
//---------------------------------------------------------------------------

#ifndef __TARGET_H__
#define __TARGET_H__

#include "linux/workqueue.h"

#define FALSE				0
#define TRUE				1

#define TARGET_TYPE_NONE				0
#define TARGET_TYPE_LIFTER				1
#define TARGET_TYPE_MOVER				2
#define TARGET_TYPE_HIT_SENSOR			3
#define TARGET_TYPE_MUZZLE_FLASH		4
#define TARGET_TYPE_MILES_TRANSMITTER	5
#define TARGET_TYPE_SOUND				6
#define TARGET_TYPE_THERMAL				7
#define TARGET_TYPE_BATTERY				8
#define TARGET_TYPE_USER_INTERFACE		9
#define TARGET_TYPE_SES_INTERFACE		10
#define TARGET_TYPE_MOVER_POSITION		11
#define TARGET_TYPE_SCOTT_TEST		12

struct target_device
	{
	int 							type;
	char							*name;
	struct device           		*dev;
	const struct attribute_group* 	(*get_attr_group)(void);
	};

extern int 	target_sysfs_add	(struct target_device * target_device);
extern void target_sysfs_remove	(struct target_device * target_device);
extern void target_sysfs_notify	(struct target_device * target_device, char * attribute_name);
extern void target_flush_work(struct work_struct *work);

extern struct atmel_tc * 	target_timer_alloc	(unsigned block, const char *name);
extern void 				target_timer_free	(struct atmel_tc *tc);

extern void 				target_hrtimer_init			(struct hrtimer *timer, clockid_t which_clock,  enum hrtimer_mode mode);
extern int 					target_hrtimer_start		(struct hrtimer *timer, ktime_t tim, const enum hrtimer_mode mode);
extern int 					target_hrtimer_cancel		(struct hrtimer *timer);
extern u64					target_hrtimer_forward_now	(struct hrtimer *timer, ktime_t interval);

#endif // __TARGET_H__
