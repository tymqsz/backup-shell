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

void init_workerList(workerList** workers);

void display_workerList(workerList* workers);

void child_work(char* src, char* dst);

int synchro_present(char* src, char* dst, workerList* workers);

int dst_is_subdir(char* src, char* dst);

int verify_src_dst(char* src, char* dst, workerList* workers);

#endif