#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <fcntl.h>

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

void delete_workers_by_pid(pid_t pid, workerList* workers) {
    if (workers == NULL || workers->size == 0) return;

    char* target_src = NULL;

    // 1. Find the Source path associated with the given PID
    for (int i = 0; i < workers->size; i++) {
        if (workers->list[i].pid == pid) {
            // Duplicate the string so we can safely compare it 
            // even after freeing the specific worker entry.
            if (workers->list[i].source != NULL) {
                target_src = strdup(workers->list[i].source);
            }
            break;
        }
    }

    // If PID not found or source was NULL, exit
    if (target_src == NULL) {
        return;
    }

    // 2. Iterate through the list and filter out matches
    int write_idx = 0; // The position where we keep valid workers

    for (int i = 0; i < workers->size; i++) {
        
        // Check if the current worker shares the same source
        if (strcmp(workers->list[i].source, target_src) == 0) {
            free(workers->list[i].source);
            if (workers->list[i].destination != NULL) {
                free(workers->list[i].destination);
            }

        } else {
        
            if (i != write_idx) {
                workers->list[write_idx] = workers->list[i];
            }
            write_idx++;
        }
    }
    workers->size = write_idx;

    free(target_src);
}

void delete_workers_by_paths(char* src, char** dsts, workerList* workers) {
    if (workers == NULL || workers->size == 0 || src == NULL || dsts == NULL){
        printf("lipton");
        return;
    } 

    int write_idx = 0;

    for (int i = 0; i < workers->size; i++) {
        int should_delete = 0;

        // 1. Check if source matches
        if (strcmp(workers->list[i].source, src) == 0) {
            
            // 2. Check if the worker's destination exists in the provided dsts list
            int j = 0;
            while (dsts[j] != NULL) {
                if (strcmp(workers->list[i].destination, dsts[j]) == 0) {
                    should_delete = 1;
                    break;
                }
                j++;
            }
        }

        if (should_delete) {
            free(workers->list[i].source);
            free(workers->list[i].destination);

        } else {
            if (i != write_idx) {
                workers->list[write_idx] = workers->list[i];
            }
            write_idx++;
        }
    }
    workers->size = write_idx;
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

    kill(getppid(), SIGUSR1);
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