#ifndef SYNC_H
#define SYNC_H

#include "worker.h"

typedef struct WatchMap {
    int wd;
    char *path;
    struct WatchMap *next;
} WatchMap;

void synchronize(const char *, const char *);

void restore(const char *, const char *);

int prep_dirs(char*, char*, workerList*);

#endif