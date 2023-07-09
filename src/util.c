#include "util.h"

int antoi(const char *buf, size_t len)
{
    int n = 0;

    while (len--) {
        n = n*10 + *buf++ - '0';
    }

    return n;
}

