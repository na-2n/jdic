#ifndef __ARRAY_H__
#define __ARRAY_H__

#include <stdlib.h>

typedef struct {
    size_t size;
    size_t tsize;
    size_t asize;
    void *ptr;
} array_t;

typedef void (*array_free_func)(void *ptr);

array_t array_new(size_t, size_t);
int array_check(array_t *, size_t);
void array_free(array_t *, array_free_func f);
#define ARRAY(x, type) ((type*)(x->ptr))

#endif // __ARRAY_H__

