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
#include "utils.h"


/* add worker to workers provided as an argument */
void add_worker(char* src, char* dst, pid_t pid, workerList* workers){
    /* resize if necessary */
    if(workers->size >= workers->capacity){
        worker* new_list = realloc(workers->list, (workers->capacity+16)*sizeof(worker));
        if(new_list == NULL)
            ERR("realloc");
        
        workers->list = new_list;
        workers->capacity += 16;
    }
    
    workers->list[workers->size].source = strdup(src);
    workers->list[workers->size].destination = strdup(dst);
    workers->list[workers->size].pid = pid;

    workers->size++;
}

/*  delete a worker with a given pid
 *  returns: 0 - success, -1 failure
 */
int delete_workers_by_pid(pid_t pid, workerList* workers) {
   
    if (workers == NULL || workers->size == 0) 
        return -1;

    int worker_idx = -1;
    int i = 0, worker_cnt = workers->size;
    while(i < worker_cnt){
        if(workers->list[i].pid == pid){
            worker_idx = i;
            break;
        }
        i++;
    }

    /* worker with this pid not found*/
    if(worker_idx == -1){
        printf("here");
        return -1;
    }

    /* overwrite the worker*/
    if(worker_idx != worker_cnt -1){
        if(memmove(workers->list+worker_idx, workers->list+worker_idx+1, (worker_cnt-worker_idx-1)* sizeof(worker)) == NULL){
            ERR("memmove");
        }
    }

    
    workers->size--;
    return 0;
}

/*  delete a worker with a given src and one of dsts
 *  returns: 0 - success, -1 failure
 */
int delete_workers_by_paths(char* src, char** dsts, workerList* workers) {
    if (workers == NULL || workers->size == 0 || src == NULL || dsts == NULL){
        return -1;
    } 

    int write_idx = 0;
    for (int i = 0; i < workers->size; i++) {
        int should_delete = 0;

        if (strcmp(workers->list[i].source, src) == 0) {

            /* check if one of destination matches */
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
            kill(workers->list[i].pid, SIGTERM);
            free(workers->list[i].source);
            free(workers->list[i].destination);

        } else {
            /* overwrite killed workers */
            if (i != write_idx) {
                workers->list[write_idx] = workers->list[i];
            }
            write_idx++;
        }
    }
    workers->size = write_idx;

    return 0;
}

/* initialize workers */
void init_workerList(workerList** workers){
    *workers = malloc(sizeof(workerList));
    (*workers)->capacity = 16;
    (*workers)->size = 0;
    (*workers)->list = malloc(sizeof(worker) * (*workers)->capacity);
}


void display_workerList(workerList* workers){
    for(int i = 0; i < workers->size; i++){
        printf("[%d], backup %d: %s -> %s \n", i, workers->list[i].pid,
            (workers->list[i]).source, (workers->list[i]).destination);
    }
}

/* start backup from src to dst path */
void backup_work(char* src, char* dst){
    setup_target_dir(dst);
    start_copy(src, dst);
    synchronize(src, dst);
}