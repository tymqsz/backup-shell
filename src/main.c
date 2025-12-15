#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <sys/wait.h>


#include "utils.h"
#include "fileproc.h"
#include "worker.h"
#include "utils.h"
#include "synchro.h"


volatile sig_atomic_t END = 0;

void handleInterruption(int sig){
    /* end of app */
    END = 1;
}


void collectDeadWorkers(workerList* workers){
    pid_t pid;
    int worker_cnt = workers->size, i = 0;

    while(i < worker_cnt){
        pid  = waitpid(-1, NULL, WNOHANG);
        if(pid <= 0)
            return;
        
        delete_workers_by_pid(pid, workers);
        i++;
    }
}

int main(){
    sigset_t mask;
    sigfillset(&mask); 
    sigdelset(&mask, SIGINT);
    sigdelset(&mask, SIGTERM);
    sigprocmask(SIG_SETMASK, &mask, NULL);

    setHandler(handleInterruption, SIGINT);
    setHandler(handleInterruption, SIGTERM);
    
    workerList* workers;
    init_workerList(&workers);


    char line[2 * PATH_MAX + 50];
    while (!END) {
        printf("command: ");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }
        line[strcspn(line, "\n")] = 0;

        int argc;
        char** argv = split_line(line, &argc);
        if(argv == NULL || argc == 0)
            continue;

        char* cmd = argv[0];
        
        /* check if any of workers died */
        collectDeadWorkers(workers);

        if(strcmp(cmd, "add") == 0){
            if(argc < 3){
                printf("Usage: add <source_dir> <target_dirs>\n");
                free(argv);
                continue;
            }
            
            for(int i = 2; i < argc; i++) {
                if(prep_dirs(argv[1], argv[i], workers) == -1){
                    continue;
                }
                
                pid_t pid = fork();
                if (pid < 0) {
                    ERR("fork");
                } 
                else if(pid == 0){
                    setHandler(SIG_DFL, SIGTERM);
                    backup_work(argv[1], argv[i]);
                    exit(EXIT_SUCCESS);
                }
                
                add_worker(argv[1], argv[i], pid, workers);
            }
        }
        else if(strcmp(cmd, "end") == 0){            
            delete_workers_by_paths(argv[1], argv+2, workers);
        }
        else if(strcmp(cmd, "list") == 0){
            display_workerList(workers);
        }
        else if(strcmp(cmd, "exit") == 0){
            free(argv);
            break;
        }
        else if(strcmp(cmd, "restore") == 0){
            restore(argv[2], argv[1]);
        }
        else{
            printf("command unknown\n");
        }
        
        free(argv); 
    }
    
    delete_all_workers(workers);
    kill(0, SIGTERM);
    return 0;
}