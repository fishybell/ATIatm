#ifndef __MD5SUM_H__
#define __MD5SUM_H__

// gets the md5sum for a file, puts it in dest (which needs to be at least 16 bytes long)
int md5sum(char *file, void *dest); // returns 0 on fail, 1 on success
int md5sum_data(void *data, int size, void *dest); // returns 0 on fail, 1 on success

#endif
