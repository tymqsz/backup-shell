#ifndef WH
#define WH

typedef struct Worker{
    char* source;
    char* destination;
    pid_t pid;
} worker;

typedef struct WorkerList{
    int capacity;
    int size;
    worker* list;
} workerList;

void add_worker(char* src, char* dst, pid_t pid, workerList* workers);

int delete_workers_by_pid(pid_t pid, workerList* workers);

int delete_workers_by_paths(char* src, char** dsts, workerList* workers);

void init_workerList(workerList** workers);

void display_workerList(workerList* workers);

void backup_work(char* src, char* dst);

#endif