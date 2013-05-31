//---------------------------------------------------------------------------
// eeprom_settings.c
//---------------------------------------------------------------------------

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>

#include <linux/device.h>
#include "delay_printk.h"
#include "eeprom_settings.h"

#define MAX_SIZE 512

struct file *eepromFile = NULL;
int eepromFd = -1;
char sbuf[MAX_SIZE];

//---------------------------------------------------------------------------
// This atomic variable is use to indicate that we are fully initialized
//---------------------------------------------------------------------------
atomic_t full_init = ATOMIC_INIT(0);

//#define PRINT_DEBUG

#ifdef PRINT_DEBUG
#define DELAY_PRINTK  delay_printk
#else
#define DELAY_PRINTK(...)  //
#endif

static char *read_eeprom_setting(int addr, int size){
    mm_segment_t oldfs;
    int retval;
    char *buf = sbuf;
    unsigned long long offset;
    char *filename = "/sys/class/i2c-adapter/i2c-0/0-0050/eeprom"; // 0x50 is the eeprom's i2c address
    offset = (unsigned long long)addr;
    DELAY_PRINTK("read_eeprom_setting: starting addr:%i size:%i\n", addr, size);
    
    oldfs = get_fs();
    DELAY_PRINTK("read_eeprom_setting: set_fs\n");
    set_fs(KERNEL_DS);
    eepromFile = filp_open(filename, READ, MAY_READ);
    // read buffer from eeprom
    DELAY_PRINTK("read_eeprom_setting: vfs_read\n");
    retval=vfs_read(eepromFile, buf, size, &offset);
    DELAY_PRINTK("read_eeprom_setting: set_fs\n");
    filp_close(eepromFile, NULL);
    set_fs(oldfs);
    DELAY_PRINTK("read_eeprom_setting:%s:\n", sbuf);
    return sbuf;
}

int get_eeprom_int_value(int defaultValue, int addr, int size){
    int eeValue;
    char **endp;
    char *buf = read_eeprom_setting(addr, size);
    if (buf != sbuf) return -1;
    if (sbuf[0] != 0) eeValue = (int)simple_strtol(buf, endp, 10);
    else eeValue = defaultValue;
    return eeValue;
}
EXPORT_SYMBOL(get_eeprom_int_value); 

//---------------------------------------------------------------------------
// init handler for the module
//---------------------------------------------------------------------------
static int __init eeprom_setting_init(void) {
    int retval = 0;
    atomic_set(&full_init, 1);
    return retval;
}

//---------------------------------------------------------------------------
// exit handler for the module
//---------------------------------------------------------------------------
static void __exit eeprom_setting_exit(void) {
    atomic_set(&full_init, 0);
    eepromFile = NULL;
}


//---------------------------------------------------------------------------
// module declarations
//---------------------------------------------------------------------------
module_init(eeprom_setting_init);
module_exit(eeprom_setting_exit);
MODULE_LICENSE("proprietary");
MODULE_DESCRIPTION("ATI eeprom settings module");
MODULE_AUTHOR("rn");



