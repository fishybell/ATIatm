#include <stdio.h>
#include "md5sum.h"

int main(int argc, char **argv) {
   unsigned int dest[4];
   if (argc != 2) {
      printf("please provide file name\n");
      return(1);
   }
   printf("looking at file %s\n", argv[1]);
   if (md5sum(argv[1], dest)) {
      printf("found md5sum: %08x%08x%08x%08x\n", dest[0], dest[1], dest[2], dest[3]);
   } else {
      printf("failed to calculate\n");
      return(1);
   }
   if (md5sum_data(dest, 16, dest)) {
      printf("found md5sum data: %08x%08x%08x%08x\n", dest[0], dest[1], dest[3], dest[3]);
   } else {
      printf("failed to calculate data\n");
      return(1);
   }
}


