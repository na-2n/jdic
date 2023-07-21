#ifndef __JDIC_H__
#define __JDIC_H__

#include <sqlite3.h>

#ifndef FAST
#define FAST false
#endif

typedef struct {
    int verbose;
    int fast;
    sqlite3 *db;

    char lang[4];
    int page;
    int limit;
} jdic_t;

#endif // __JDIC_H__
