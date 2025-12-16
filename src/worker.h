#ifndef WH
#define WH

#include <stddef.h>
#include <sys/types.h>
#include "fileproc.h"
#include "utils.h"
typedef struct Worker
{
    char *source;
    char *destination;
    pid_t pid;
} worker;

typedef struct WorkerList
{
    int capacity;
    int size;
    worker *list;
} workerList;

void add_worker(char *, char *, pid_t, workerList *);

void delete_all_workers(workerList *);

int delete_workers_by_pid(pid_t, workerList *);

int delete_workers_by_paths(char *, char **, workerList *);

void init_workerList(workerList **);

void display_workerList(workerList *);

void backup_work(char *, char *);

#endif
