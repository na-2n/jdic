#include <stdio.h>
#include <stdlib.h>

#include "array.h"

array_t array_new(size_t isize, size_t tsize)
{
    array_t arr = {
        .tsize = tsize,
        .asize = isize * tsize,
        .ptr = calloc(isize, tsize),
    };

    return arr;
}

int array_check(array_t *arr, size_t nsize)
{
    if (arr->asize < nsize) {
        void *new = realloc(arr->ptr, arr->asize * 2);
        if (new == NULL) {
            return 0;
        }

        arr->ptr = new;
        arr->asize *= 2;
    }

    return 1;
}

void array_free(array_t *arr, array_free_func f)
{
    if (arr != NULL && arr->ptr != NULL) {
        if (f != NULL) {
            for (int i = 0; i < arr->size; i++) {
                void *loc = arr->ptr + (arr->tsize * (unsigned long)i);
                f(loc);
            }
        }

        free(arr->ptr);
    }
}

