#include <stdio.h>
#include <fcntl.h>

#define READ 0
#define WRITE 1
#define ERROR 2

static char *dirName[] = {
    "read",
    "write",
    "error"
};

#define MAX_SIZE 512

int main(int argc, char *argv[]) {

    int i, retval, addr=0x0, size=0x40;
    int direction = ERROR;

    char buf[MAX_SIZE];

    char *file = "/sys/class/i2c-adapter/i2c-0/0-0050/eeprom";
    char *usage = "Usage: %s (read|write) [-addr ##] [-size ##] [-file XX]\n";

    // loop over arguments
    for (i=1; i<argc; i++) {
       if((retval=strncmp("write", argv[i], 5)) == 0) {
           direction = WRITE;
       } else if((retval=strncmp("read", argv[i], 4)) == 0) {
           direction = READ;
        } else if((retval=strncmp("-file", argv[i], 5)) == 0 && (i+1<argc)) {
           file = argv[++i];
       } else if((retval=strncmp("-addr", argv[i], 5)) == 0 && (i+1<argc)) {
           i++;
           if(argv[i][0] == '0' && (argv[i][1] == 'x' || argv[i][1] == 'X')) {
               if((retval=sscanf(argv[i]+2,"%x",&addr)) != 1) {
                   error_at_line(-1,0,__FILE__,__LINE__,"invalid hexadecimal number for address: %s\n", argv[i]);
                   return -1;
               }
           } else {
               if((retval=sscanf(argv[i],"%i",&addr)) != 1) {
                   if((retval=sscanf(argv[i],"%x",&addr)) != 1) {
                       error_at_line(-1,0,__FILE__,__LINE__,"invalid number for address: %s\n", argv[i]);
                       return -1;
                   }
               }
           }
       } else if((retval=strncmp("-size", argv[i], 5)) == 0 && (i+1<argc)) {
           i++;
           if(argv[i][0] == '0' && (argv[i][1] == 'x' || argv[i][1] == 'X')) {
               if((retval=sscanf(argv[i]+2,"%x",&size)) != 1) {
                   error_at_line(-1,0,__FILE__,__LINE__,"invalid hexadecimal number for size: %s\n", argv[i]);
                   return -1;
               }
           } else {
               if((retval=sscanf(argv[i],"%i",&size)) != 1) {
                   if((retval=sscanf(argv[i],"%x",&size)) != 1) {
                       error_at_line(-1,0,__FILE__,__LINE__,"invalid number for size: %s\n", argv[i]);
                       return -1;
                   }
               }
           }
           if(size > MAX_SIZE) {
               error_at_line(-1,0,__FILE__,__LINE__,"invalid number for size: 0x%20X (max size is %i)\n", size, MAX_SIZE);
           }
        } else {
           error_at_line(-1,0,__FILE__,__LINE__,usage, argv[0]);
           return -1;
        }
    }

    int fd;

    // error out or open the file for reading or writing
    switch (direction) {
        default:
        case ERROR:
            error_at_line(-1,0,__FILE__,__LINE__,usage, argv[0]);
            return -1;
            break;

        case READ:
            fd = open(file, O_RDONLY);
            break;

        case WRITE:
            fd = open(file, O_WRONLY);
            break;
    }

    if(fd == -1) {
        close(fd);
        error_at_line(-1,0,__FILE__,__LINE__,"Could open file %s\n", file);
    }
    
    if((retval=lseek(fd, addr, SEEK_SET)) != addr) {
        close(fd);
        error_at_line(-1,0,__FILE__,__LINE__,"Could not seek to address 0x%02X\n", addr);
    }

    switch (direction) {
        case READ:
            if((retval=read(fd, buf, size)) != size) {
                close(fd);
                error_at_line(-1,0,__FILE__,__LINE__,"Could not read full 0x%02X bytes\n", size);
            }
            fwrite(buf, sizeof(char), size, stdout);
            break;
        case WRITE:
            if((retval=fread(buf, sizeof(char), size, stdin)) != size) {
                close(fd);
printf("retval: %i\nbuf: %s\n", retval, buf);
                error_at_line(-1,0,__FILE__,__LINE__,"Could not read full 0x%02X bytes from stdin\n", size);
            }
            if((retval=write(fd, buf, size)) != size) {
                close(fd);
                error_at_line(-1,0,__FILE__,__LINE__,"Could not write full 0x%02X bytes\n", size);
            }
            break;
    }
    close(fd);
}
