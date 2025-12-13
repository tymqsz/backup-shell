#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <limits.h>

#include "worker.h"
#include "synchro.h"
#include "fileproc.h"

void add_worker(char* src, char* dst, pid_t pid, workerList* workers){
    if(workers->size >= workers->capacity){
        worker* new_list = realloc(workers->list, (workers->capacity+16)*sizeof(worker));
        if(new_list == NULL)
            exit(EXIT_FAILURE);
        
        workers->list = new_list;
        workers->capacity += 16;
    }
    
    workers->list[workers->size].source = strdup(src);
    workers->list[workers->size].destination = strdup(dst);
    workers->list[workers->size].pid = pid;

    workers->size++;
}

void init_workerList(workerList** workers){
    *workers = malloc(sizeof(workerList));
    (*workers)->capacity = 16;
    (*workers)->size = 0;
    (*workers)->list = malloc(sizeof(worker) * (*workers)->capacity);
}

void display_workerList(workerList* workers){
    for(int i = 0; i < workers->size; i++){
        printf("synch: %s | %s \n", (workers->list[i]).source, (workers->list[i]).destination);
    }
}



void child_work(char* src, char* dst){
    setup_target_dir(dst);

    start_copy(src, dst);
    
    synchronize(src, dst);
}

int synchro_present(char* src, char* dst, workerList* workers){
    for(int i = 0; i < workers->size; i++){
        if(
            strcmp((workers->list[i]).source, src) == 0 && 
            strcmp((workers->list[i]).destination, dst) == 0
        )
            return 1;
    }

    return 0;
}

int dst_is_subdir(char* src, char* dst){
    return strstr(dst, src) != NULL;
}

int verify_src_dst(char* src, char* dst, workerList* workers){
    struct stat src_stat;
    if (stat(src, &src_stat) < 0)
        return 0;
    if (!S_ISDIR(src_stat.st_mode))
        return 0;

    return (
        !synchro_present(src, dst, workers) &&
        !dst_is_subdir(src, dst)
    );
}