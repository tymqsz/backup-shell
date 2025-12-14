#ifndef SYNC_H
#define SYNC_H

#include "worker.h"

typedef struct WatchMap {
    int wd;
    char *path;
    struct WatchMap *next;
} WatchMap;

void synchronize(const char *source_dir, const char *target_dir);

void restore(const char *backup_dir, const char *restore_dir);

int prep_dirs(char* src, char* dst, workerList* workers);

#endif