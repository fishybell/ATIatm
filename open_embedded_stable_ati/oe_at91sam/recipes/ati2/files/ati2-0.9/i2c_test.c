#include <fcntl.h>
//#include <linux/i2c.h>
//#include <linux/i2c-dev.h>
#include "i2c-dev.hh"
#include <stdio.h>

int main(int argc, char **argv) {
    int file;
    int adapter_nr = 0; /* probably dynamically determined */
    char filename[20];
    
    snprintf(filename, 19, "/dev/i2c-%d", adapter_nr);
    file = open(filename, O_RDWR);
    if (file < 0) {
        /* ERROR HANDLING; you can check errno to see what went wrong */
        return 1;
    }

    int addr = 0x50; /* The I2C address */
    
    if (ioctl(file, I2C_SLAVE, addr) < 0) {
        /* ERROR HANDLING; you can check errno to see what went wrong */
        return 1;
    }

    __u8 reg = 0x10; /* Device register to access */
    __s32 res;
    char buf[10];

    /* Using I2C Write, equivalent of 
       i2c_smbus_write_word_data(file, register, 0x6543) */
    buf[0] = reg;
    buf[1] = 0x43; // write to address
    buf[2] = 0x65; // data to write
    res = i2c_smbus_write_byte_data(file, buf[1], buf[2]);
    if (res == -1) {printf("error on write\n");} else {printf("success on write\n");}
//    if (write(file, buf, 3) != 3) {
//      /* ERROR HANDLING: i2c transaction failed */
//      printf("error on write\n");
//      return 1;
//    }

    /* Using I2C Read, equivalent of i2c_smbus_read_byte(file) */
    unsigned char result;
    res = i2c_smbus_write_byte(file, buf[1]); // dummy write to read
    if (res == -1) {printf("error on write\n");} else {printf("success on write\n");}
    result = i2c_smbus_read_byte(file); // actual read
    if (result == -1) {printf("error on read\n");} else {printf("success on read\n");}
    printf("Read byte: 0x%2X\n", result);

//    int retval;
//    if ((retval=read(file, buf, 1)) != 1) {
//      /* ERROR HANDLING: i2c transaction failed */
//      printf("error on read: %i\n", retval);
//      return 1;
//     } else {
//      /* buf[0] contains the read byte */
//      printf("Read byte: 0x%2X\n", buf[0]);
//    }

    return 0;
}
