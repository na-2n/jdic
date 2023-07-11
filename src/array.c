#include <stdio.h>
#include <stdlib.h>

#include "array.h"

array_t * array_new(size_t isize, size_t tsize)
{
    array_t *arr = calloc(1, sizeof(array_t));
    arr->asize = isize;
    arr->tsize = tsize;
    arr->ptr = calloc(isize, tsize);

    return arr;
}

int array_check(array_t *arr, size_t nsize)
{
    printf("arr->asize = %zu\n", arr->asize);
    printf("nsize = %zu\n", nsize);
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

void array_free(array_t *arr)
{
    if (arr != NULL) {
        if (arr->ptr != NULL) free(arr->ptr);
        free(arr);
    }
}

