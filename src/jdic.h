#ifndef __JDIC_H__
#define __JDIC_H__

#include <sqlite3.h>

typedef struct {
    int verbose;
    sqlite3 *db;

    int page;
    int limit;
} jdic_t;

#endif // __JDIC_H__
