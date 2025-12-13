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
#include "worker.h"

int main(){
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
                    child_work(arg1, arg2);
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

    return 0;
}