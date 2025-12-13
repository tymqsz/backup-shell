#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <limits.h>

#include "fileproc.h"
#include "synchro.h"

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


void add_worker(char* src, char* dst, pid_t pid, workerList* workers){
    printf("SIZE: %d\n", workers->size);
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



void child_process(char* src, char* dst){
    setup_dirs(src, dst);

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
    /* add src presence verification */
    return (
        !synchro_present(src, dst, workers) &&
        !dst_is_subdir(src, dst)
    );
}

void main_process(){
    char input_line[2 * PATH_MAX + 50]; 
    char command[10];
    char arg1[PATH_MAX], arg2[PATH_MAX];

    workerList* workers;
    init_workerList(&workers);

    while (1) {
        printf("SyncShell> ");
        if (fgets(input_line, sizeof(input_line), stdin) == NULL) {
            break;
        }
        input_line[strcspn(input_line, "\n")] = 0;


        if (sscanf(input_line, "%9s %s %[^\n]", command, arg1, arg2) >= 1) {
            
            if (strcmp(command, "add") == 0) {
                pid_t pid = fork();
                if(pid < 0)
                    exit(EXIT_FAILURE);
                else if(pid == 0){
                    child_process(arg1, arg2);
                    exit(EXIT_SUCCESS);
                }
                
                if(verify_src_dst(arg1, arg2, workers))
                    add_worker(arg1, arg2, pid, workers);
                else
                    printf("incorr\n");
            } 
            else if (strcmp(command, "end") == 0) {
                printf("end\n");
            } 
            else if (strcmp(command, "list") == 0) {
                display_workerList(workers);
            } 
            else if (strcmp(command, "exit") == 0) {
                break; 
            }
            else {
                fprintf(stderr, "Nieznane polecenie. Użyj: add, end, list, restore, exit.\n");
            }

        } else if (strcmp(input_line, "list") == 0) {
        } else {
            fprintf(stderr, "Niepoprawny format polecenia.\n");
        }
    }
}


int main(){

    main_process();

    return 0;
}