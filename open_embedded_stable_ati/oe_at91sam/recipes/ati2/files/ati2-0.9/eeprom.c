#include <stdio.h>
#include <fcntl.h>
#include <string.h>

#define READ 0
#define WRITE 1
#define ERROR 2

#define MAX_SIZE 512

int main(int argc, char *argv[]) {

    int i, retval, fd, addr=0x0, size=0x40, blank=0x40;
    int direction = ERROR;

    char sbuf[MAX_SIZE];
    char sbbuf[MAX_SIZE];

    char *buf = sbuf;
    char *bbuf = sbbuf;

    char *file = "/sys/class/i2c-adapter/i2c-0/0-0050/eeprom"; // 0x50 is the eeprom's i2c address
    char *usage = "Usage: %s (read|write) [-addr ##] [-size ##] [-file XX]\n";

    // loop over arguments
    for (i=1; i<argc; i++) {
       if((retval=strncmp("write", argv[i], 5)) == 0) {
           // we're going to write
           direction = WRITE;
       } else if((retval=strncmp("read", argv[i], 4)) == 0) {
           // we're going to read
           direction = READ;
       } else if((retval=strncmp("-file", argv[i], 5)) == 0 && (i+1<argc)) {
           // we have a different address for the eeprom device
           file = argv[++i];
       } else if((retval=strncmp("-blank", argv[i], 6)) == 0 && (i+1<argc)) {
           i++;
           // read address as hex
           if(argv[i][0] == '0' && (argv[i][1] == 'x' || argv[i][1] == 'X')) {
               if((retval=sscanf(argv[i]+2,"%x",&blank)) != 1) {
                   error_at_line(-1,0,__FILE__,__LINE__,"invalid hexadecimal number for blank: %s\n", argv[i]);
               }
           } else {
               // read address as integer
               if((retval=sscanf(argv[i],"%i",&blank)) != 1) {
                   // read address as hex
                   if((retval=sscanf(argv[i],"%x",&blank)) != 1) {
                       error_at_line(-1,0,__FILE__,__LINE__,"invalid number for blank: %s\n", argv[i]);
                   }
               }
           }
           // don't overflow our buffer
           if(blank > MAX_SIZE) {
               error_at_line(-1,0,__FILE__,__LINE__,"invalid number for blank: 0x%20X (max blank size is %i)\n", size, MAX_SIZE);
           }
       } else if((retval=strncmp("-addr", argv[i], 5)) == 0 && (i+1<argc)) {
           i++;
           // read address as hex
           if(argv[i][0] == '0' && (argv[i][1] == 'x' || argv[i][1] == 'X')) {
               if((retval=sscanf(argv[i]+2,"%x",&addr)) != 1) {
                   error_at_line(-1,0,__FILE__,__LINE__,"invalid hexadecimal number for address: %s\n", argv[i]);
               }
           } else {
               // read address as integer
               if((retval=sscanf(argv[i],"%i",&addr)) != 1) {
                   // read address as hex
                   if((retval=sscanf(argv[i],"%x",&addr)) != 1) {
                       error_at_line(-1,0,__FILE__,__LINE__,"invalid number for address: %s\n", argv[i]);
                   }
               }
           }
       } else if((retval=strncmp("-size", argv[i], 5)) == 0 && (i+1<argc)) {
           i++;
           // read size as hex
           if(argv[i][0] == '0' && (argv[i][1] == 'x' || argv[i][1] == 'X')) {
               if((retval=sscanf(argv[i]+2,"%x",&size)) != 1) {
                   error_at_line(-1,0,__FILE__,__LINE__,"invalid hexadecimal number for size: %s\n", argv[i]);
               }
           } else {
               // read size as integer
               if((retval=sscanf(argv[i],"%i",&size)) != 1) {
                   // read size as hex
                   if((retval=sscanf(argv[i],"%x",&size)) != 1) {
                       error_at_line(-1,0,__FILE__,__LINE__,"invalid number for size: %s\n", argv[i]);
                   }
               }
           }
           // don't overflow our buffer
           if(size > MAX_SIZE) {
               error_at_line(-1,0,__FILE__,__LINE__,"invalid number for size: 0x%20X (max size is %i)\n", size, MAX_SIZE);
           }
        } else {
           // bad command line argument
           error_at_line(-1,0,__FILE__,__LINE__,usage, argv[0]);
        }
    }

    // error out or open the file for reading or writing
    switch (direction) {
        default:
        case ERROR:
            // we didn't get a "read" or "write" on the command line
            error_at_line(-1,0,__FILE__,__LINE__,usage, argv[0]);
            break;

        case READ:
            fd = open(file, O_RDONLY);
            break;

        case WRITE:
            fd = open(file, O_WRONLY);
            break;
    }

    // check file descriptor
    if(fd == -1) {
        close(fd);
        error_at_line(-1,0,__FILE__,__LINE__,"Could open file %s\n", file);
    }
    
    // move to addr address of eeprom
    if((retval=lseek(fd, addr, SEEK_SET)) != addr) {
        close(fd);
        error_at_line(-1,0,__FILE__,__LINE__,"Could not seek to address 0x%02X\n", addr);
    }

    switch (direction) {
        case READ:
            // read buffer from eeprom
            while ((retval=read(fd, buf, size)) != size) {
                if (size <= 0) {
                    close(fd);
                    error_at_line(-1,0,__FILE__,__LINE__,"Could not read full 0x%02X bytes\n", size);
                }
                buf = (buf) + retval;
                size -= retval;
            }
            // write buffer to stdout
            fwrite(buf, sizeof(char), size, stdout);
            break;
        case WRITE:
            // read buffer from stdin
            if((retval=fread(buf, sizeof(char), size, stdin)) != size) {
                // check for a read without a null-terminator
                if (retval != size-1 && buf[retval] != '\0') {
                    close(fd);
                    error_at_line(-1,0,__FILE__,__LINE__,"Could not read full 0x%02X bytes from stdin\n", size);
                } else {
                    buf[retval] = '\0'; // null-terminate string
                }
            }
            // blank
            if(blank > 0) {
                memset(bbuf, 0, blank);
                while ((retval=write(fd, bbuf, blank)) != blank) {
                    if (retval == 0) {
                        close(fd);
                        error_at_line(-1,0,__FILE__,__LINE__,"Could not blank full 0x%02X bytes\n", blank);
                    }
                    bbuf = (bbuf)+retval;
                    blank -= retval;
                }
                // move to addr address of eeprom
                if((retval=lseek(fd, addr, SEEK_SET)) != addr) {
                    close(fd);
                    error_at_line(-1,0,__FILE__,__LINE__,"Could not re-seek to address 0x%02X\n", addr);
                }
            }
            // write buffer to eeprom
            while ((retval=write(fd, buf, size)) != size) {
                if (retval <= 0) {
                    close(fd);
                    error_at_line(-1,0,__FILE__,__LINE__,"Could not write full 0x%02X bytes\n", size);
                }
                buf = (buf)+retval;
                size -= retval;
            }
            break;
    }

    // clean up and exit
    close(fd);
    return 0;
}

