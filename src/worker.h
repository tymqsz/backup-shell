#ifndef WH
#define WH

typedef struct Worker{
    char* source;
    char** destinations;
    int dst_count;
    pid_t pid;
} worker;

typedef struct WorkerList{
    int capacity;
    int size;
    worker* list;
} workerList;

void add_worker(char* src, char** dsts, int dst_cnt, pid_t pid, workerList* workers);

void init_workerList(workerList** workers);

void display_workerList(workerList* workers);

void child_work(char* src, char** dsts);

int synchro_present(char* src, char* dst, workerList* workers);

int dst_is_subdir(char* src, char* dst);

int prep_dirs(char*, char** dsts, workerList* workers);

#endif