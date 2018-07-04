#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <myarray.h>

typedef struct myArray myArray;

myArray * myarray_create(void) {
  myArray * temp= kmalloc(sizeof(myArray));
  if (!temp) return temp;
  temp->len = 0;
  temp->capacity = 10;
  temp->arr = kmalloc(10 * sizeof(int));
  if (!temp->arr)
  {
    kfree(temp);
    return NULL;
  }
  return temp;
}

int myarray_insert(myArray * array, int x) {
  if (!(array->capacity - array->len))
  {
    int * temp = kmalloc(2 * array->capacity * sizeof(int));
    if (!temp) return ENOMEM;
    
    for (int i = 0; i < array->capacity; i++)
    {
      temp[i] = array->arr[i];
    }
    
    kfree(array->arr);
    array->arr = temp;
    array->capacity *= 2;
  }
  
  array->arr[array->len] = x;
  ++array->len;
  return 0;
}

void myarray_delete(myArray * array)
{
  kfree(array->arr);
  kfree(array);
}
