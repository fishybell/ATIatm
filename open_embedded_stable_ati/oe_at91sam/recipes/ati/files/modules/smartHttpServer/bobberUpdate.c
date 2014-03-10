 
//#include "smartHttpServer.h"
#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <vector>
#include <string.h>
#include "../defaults.h"


static char *read_eeprom_setting(int addr, int size){
    int retval, fd;
    static char sbuf[512];
    char *buf = sbuf;
    unsigned long long offset;
    char *filename = "/sys/class/i2c-adapter/i2c-0/0-0050/eeprom"; // 0x50 is the eeprom's i2c address
    offset = (unsigned long long)addr;
    
    fd = open(filename, O_RDONLY);
    if (fd >= 0){
       if (lseek(fd, offset, SEEK_SET) == offset){
          read(fd, buf, size);
       }
       close(fd);
    }
    return sbuf;
}

void write_eeprom_setting(char *buf, int addr, int size, int blank){
    int retval, fd;
    static char sbuf[512];
    unsigned long long offset;
    char *filename = "/sys/class/i2c-adapter/i2c-0/0-0050/eeprom"; // 0x50 is the eeprom's i2c address
    offset = (unsigned long long)addr;
    
    fd = open(filename, O_WRONLY);
    if (fd >= 0){
       if (blank > 0){
          memset(sbuf, 0, 512);
          if (lseek(fd, offset, SEEK_SET) == offset){
             write(fd, sbuf, blank);
          }
       }
       if (lseek(fd, offset, SEEK_SET) == offset){
          write(fd, buf, size);
       }
       close(fd);
    }
}

int validIP(FILE *op, char *inBuff){
   int iRtnVal = 1;
   char caBuff[20], *ptr, *ptr1, *caErr;
   char caTmp[512];
   int part;
// Handle special cases
   if (strlen(inBuff) == 0) return 1;
   if (strcmp(inBuff, "dhcp") == 0) return 1;
   if (strcmp(inBuff, "DHCP") == 0) return 1;
   if (strcmp(inBuff, "0.0.0.0") == 0) return 1;
   strcpy(caBuff, inBuff);

   ptr = caBuff;
   ptr1 = strchr(ptr, '.');
   if (!ptr1) return 0;
   *ptr1 = 0;
//sprintf(caTmp, "\nvalidIP 1 -%s-\n", ptr); fwrite(caTmp, strlen(caTmp), 1, op); fflush(op);
   part = strtol(ptr, &caErr, 10);
   if (errno != 0 && part == 0) return 0;
   if (caErr == ptr) return 0;
   if (part < 0 || part > 255) return 0;

   ptr = ptr1 + 1;
   ptr1 = strchr(ptr, '.');
   if (!ptr1) return 0;
   *ptr1 = 0;
//sprintf(caTmp, "\nvalidIP 2 -%s-\n", ptr); fwrite(caTmp, strlen(caTmp), 1, op); fflush(op);
   part = strtol(ptr, &caErr, 10);
   if (errno != 0 && part == 0) return 0;
   if (caErr == ptr) return 0;
   if (part < 0 || part > 255) return 0;

   ptr = ptr1 + 1;
   ptr1 = strchr(ptr, '.');
   if (!ptr1) return 0;
   *ptr1 = 0;
//sprintf(caTmp, "\nvalidIP 3 -%s-\n", ptr); fwrite(caTmp, strlen(caTmp), 1, op); fflush(op);
   part = strtol(ptr, &caErr, 10);
   if (errno != 0 && part == 0) return 0;
   if (caErr == ptr) return 0;
   if (part < 0 || part > 255) return 0;

   ptr = ptr1 + 1;
//sprintf(caTmp, "\nvalidIP 4 -%s-\n", ptr); fwrite(caTmp, strlen(caTmp), 1, op); fflush(op);
   part = strtol(ptr, &caErr, 10);
   if (errno != 0 && part == 0) return 0;
   if (caErr == ptr) return 0;
   if (part < 0 || part > 255) return 0;

   return iRtnVal;
}

int validNUM(FILE *op, char *inBuff){
   int iRtnVal = 1;
   char caBuff[20], *ptr, *ptr1, *caErr;
   char caTmp[512];
   int part;
// Handle special cases
   if (strlen(inBuff) == 0) return 1;
   strcpy(caBuff, inBuff);

   ptr = caBuff;
sprintf(caTmp, "\nvalidNUM 1 -%s-\n", ptr); fwrite(caTmp, strlen(caTmp), 1, op); fflush(op);
   part = strtol(ptr, &caErr, 10);
   if (errno != 0 && part == 0) return 0;
   if (caErr == ptr) return 0;
sprintf(caTmp, "\nvalidNUM 2 -%s-\n", ptr); fwrite(caTmp, strlen(caTmp), 1, op); fflush(op);

   return iRtnVal;
}

void doHeader(char *h, char*b){
//   printf("1.1 200 OK\r\n");
//   printf("%s 200 OK\r\n", getenv((char *)"SERVER_PROTOCOL"));
//   printf("Connection: keep-alive\r\n");
   printf("Server: %s\r\n", getenv((char *)"SERVER_NAME"));
   printf("Date: Wed, 18 Feb 2014 09:32:03 GMT\r\n");
   printf("%s\r\n", h);
//   printf("Vary:\r\n");
   printf("Content-Length: %d\r\n", strlen(b) + 1);
   printf("\r\n\r\n");
}



int main( int argc, char **argv, char **envp ){
   int count, rc;
   char requestType[128];
   char Buffff[16384];
   char oBuffff[1024];
   char contentType[1024];
   char inBuff[4096];
   char caTmp[512];
   FILE *fp, *op;
   char **env;
   int html = 0, len=0;
   char *ptr, *ptr0, *ptr1, *ptr2;

   
   memset(inBuff, 0, 4096);
   memset(Buffff, 0, 1024);
   memset(oBuffff, 0, 1024);
/*
 * op = fopen("./tmpfile", "a");
   if (op != NULL){
   while (read(0, inBuff, 1) > 0){
      fwrite(inBuff, 1, 1, op);
      fflush(op);
   }
   fclose(op);
   }
*/
/*
   fp = fopen("ajaxfifo", "r");
   if (fp != NULL){
      fgets(Buffff, 1024, fp);
      fclose(fp);
   }
*/
   sprintf(Buffff, "\r\n\r\n");
/*
   for (env = envp; *env != 0; env ++){
      sprintf(Buffff + strlen(Buffff), "%s<br>", *env);
   }
*/
   strcpy(requestType, getenv((char *)"REQUEST_METHOD"));
//   sprintf(Buffff + strlen(Buffff), "requestType: %s<br>", requestType);
   if (strcmp(requestType, "GET") == 0){
      sprintf(Buffff + strlen(Buffff), "%s", html ? "&#123;<br>": "{"); // Start

         sprintf(Buffff + strlen(Buffff), "\"IP\": \"%s\",%s",
            read_eeprom_setting(STATIC_IP_LOC, STATIC_IP_SIZE), html ? "<br>": "");

         sprintf(Buffff + strlen(Buffff), "\"SUBNET\": \"%s\",%s",
            read_eeprom_setting(SUBNET_LOC, SUBNET_SIZE), html ? "<br>": "");

         sprintf(Buffff + strlen(Buffff), "\"BOBHITS\": \"%s\",%s",
            read_eeprom_setting(BOB_HITS_LOC, BOB_HITS_SIZE), html ? "<br>": "");

         sprintf(Buffff + strlen(Buffff), "\"SENSE\": \"%s\",%s",
            read_eeprom_setting(HIT_DESENSITIVITY_LOC, HIT_DESENSITIVITY_SIZE), html ? "<br>": "");

         sprintf(Buffff + strlen(Buffff), "\"VERSION\": \"%s\",%s",
            read_eeprom_setting(VERSION_LOC, VERSION_SIZE), html ? "<br>": "");

         sprintf(Buffff + strlen(Buffff), "\"MINRND\": \"%s\",%s",
            read_eeprom_setting(MINRND_LOC, MINRND_SIZE), html ? "<br>": "");

         sprintf(Buffff + strlen(Buffff), "\"MAXRND\": \"%s\"%s",
            read_eeprom_setting(MAXRND_LOC, MAXRND_SIZE), html ? "<br>": "");

      sprintf(Buffff + strlen(Buffff), "%s", html ? "&#125;<br>": "}"); // end Start
   } else if (1 || strcmp(requestType, "POST") == 0){

   op = fopen("./tmpfile", "w");
   if (op != NULL){
   while (read(0, inBuff + len, 1) > 0){
      fwrite(inBuff + len, 1, 1, op);
      fflush(op);
      len ++;
   }
   }

   ptr = inBuff;
//sprintf(caTmp, "\nmain 1 %s\n", inBuff); fwrite(caTmp, strlen(caTmp), 1, op); fflush(op);

   while (*ptr){
       ptr0 = strchr(ptr, '&');
       if (ptr0) *ptr0 = 0;
       else ptr0 = ptr + strlen(ptr);
       ptr2 = strchr(ptr, '=');
       if (ptr2){
          *ptr2 = 0;
sprintf(caTmp, "\nmain 2 -%s- -%s-\n", ptr, ptr2 + 1); fwrite(caTmp, strlen(caTmp), 1, op); fflush(op);
          if (strcmp(ptr, "ipadd") == 0){
             if (validIP(op, ptr2 + 1)){
                write_eeprom_setting(ptr2 + 1, STATIC_IP_LOC, strlen(ptr2 + 1), STATIC_IP_SIZE);
             }
          } else if (strcmp(ptr, "subnet") == 0){
             if (validIP(op, ptr2 + 1)){
                write_eeprom_setting(ptr2 + 1, SUBNET_LOC, strlen(ptr2 + 1), SUBNET_SIZE);
             }
          } else if (strcmp(ptr, "minrnd") == 0){
             if (validNUM(op, ptr2 + 1)){
                write_eeprom_setting(ptr2 + 1, MINRND_LOC, strlen(ptr2 + 1), MINRND_SIZE);
             }
          } else if (strcmp(ptr, "maxrnd") == 0){
             if (validNUM(op, ptr2 + 1)){
                write_eeprom_setting(ptr2 + 1, MAXRND_LOC, strlen(ptr2 + 1), MAXRND_SIZE);
             }
          } else if (strcmp(ptr, "bobhits") == 0){
             if (validNUM(op, ptr2 + 1)){
                write_eeprom_setting(ptr2 + 1, BOB_HITS_LOC, strlen(ptr2 + 1), BOB_HITS_SIZE);
             }
          } else if (strcmp(ptr, "sensitivity") == 0){
             if (validNUM(op, ptr2 + 1)){
                write_eeprom_setting(ptr2 + 1, HIT_DESENSITIVITY_LOC, strlen(ptr2 + 1), HIT_DESENSITIVITY_SIZE);
             }
          }
       }
       if (ptr0) ptr = ptr0 + 1;
       else break;
   }

   if (op != NULL) fclose(op); 
	
   if (fork() == 0){
   sleep(3);
   execl("/sbin/reboot", "/sbin/reboot", 0);
   }

   }
   
   sprintf(Buffff + strlen(Buffff), "\r\n\r\n");
   sprintf(contentType, "Content-Type: %s", html ? "text/html": "application/json");
   doHeader(contentType, Buffff);
   printf("%s\r\n", Buffff);
//   printf("\r\n\r\n");
   exit(200);
}
