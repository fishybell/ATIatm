#ifndef __TARGET_SES_INTERFACE_H__
#define __TARGET_SES_INTERFACE_H__

#include "target_hardware.h"

#define KNOB_MAX			15

#define MODE_MAINTENANCE	0
#define MODE_TESTING		1
#define MODE_RECORD		2
#define MODE_LIVEFIRE		3
#define MODE_ERROR		4
#define MODE_STOP		5 /* after error, so on error it loops back to maintenance */
#define MODE_REC_START 6 /* recording started (display visual feedback, disable mode button) */
#define MODE_ENC_START 7 /* encoding started (display visual feedback, disable mode button) */
#define MODE_REC_DONE 8 /* recording/encoding finished (display visual feedback, re-enable mode button) */
#define MODE_COPYING 9 /* copying data (unused in lower kernel) */

#endif // __TARGET_SES_INTERFACE_H__
