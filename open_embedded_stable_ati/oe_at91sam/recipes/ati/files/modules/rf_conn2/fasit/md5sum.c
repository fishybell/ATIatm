#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

size_t strnlen(const char *s, size_t maxlen);	// make the compiler happier

int md5sum(char *file, void *dest) { // returns 0 on fail, 1 on success
   char data[1024];
   char *dest_c = (char*)dest;
   int output, i;
   FILE *fp;
   int buffer_max = 256;
   int index = 0, r = 1;
   int status;
   char cmd[1024];
   snprintf(cmd, 1024, "md5sum \"%s\" | cut -d' ' -f1", file);
   memset(data, 0, 1024);

   // open pipe
   fp = popen(cmd, "r");
   if (fp == NULL) {
      printf("failed to run md5sum cmd %s\n", cmd);
      return 0;
   }

   // read/write
   while (index < 1024 && r > 0) {
      r = fread(data + index, buffer_max, sizeof(char), fp);
      index += r;
   }

   // close pipe
   status = pclose(fp);
   if (status == -1) {
      printf("failed to close md5sum cmd\n");
      return 0;
   }
	
   // copy data to dest
   output = strnlen(data, 1024);
   if (output != 33) {
      printf("failed parse md5sum cmd: %i\n<<%s>>\n", output, data);
      return 0;
   }
   for (i = 3; i >= 0; i--) {
      // starting at the end, scan the hex values into the temporary integers
      unsigned int temp;
      sscanf(data+(i*8), "%x", &temp);
      data[i*8] = '\0'; // truncate string
      dest_c[i*4+3] = (char)((temp & 0xff000000) >> 24);
      dest_c[(i*4)+2] = (char)((temp & 0xff0000) >> 16);
      dest_c[(i*4)+1] = (char)((temp & 0xff00) >> 8);
      dest_c[(i*4)] = (char)(temp & 0xff);
   }
   return 1;
}

int md5sum_data(void *data, int size, void *dest) { // returns 0 on fail, 1 on success
   int fd, ret;
   // find temporary name
   char temp_name[10];
   struct stat buf;
   do {
      int i;
      for (i = 0; i < 9; i++) {
         temp_name[i] = (rand() % 26) + 'a';
      }
      temp_name[9] = '\0';
   } while (stat(temp_name, &buf) == 0);

   // dump the data into a temp file
   fd = open(temp_name, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
   write(fd, data, size);
   close(fd);

   // md5sum it
   ret = md5sum(temp_name, dest);

   // delete the temp file
   unlink(temp_name);

   return ret;
}

