#ifndef _MYARRAY_H_
#define _MYARRAY_H_
#include <lib.h>

struct myArray {
  int len;
  int capacity;
  int * arr;
};

struct myArray * myarray_create(void);

int myarray_insert(struct myArray * array, int x);

void myarray_delete(struct myArray * array);

#endif /* _MYARRAY_H_ */
