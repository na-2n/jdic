#ifndef __JMDICT_H__
#define __JMDICT_H__

#include "jdic.h"

int jmdict_import(jdic_t *, const char *);
int jmdict_search_kanji(jdic_t *, const char *, int *);
int jmdict_search_reading(jdic_t *, const char *, int *);
int jmdict_search_definition(jdic_t *, const char *, int *);

#endif // __JMDICT_H__
