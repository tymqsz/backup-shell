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

#include "fileproc.h"
#include "synchro.h"
#include "worker.h"

char** split_line(char *line, int *count) {
    if (line[0] != '\0' && line[strlen(line) - 1] == '\n') {
        line[strlen(line) - 1] = '\0';
    }

    char *temp_line = strdup(line);
    if (temp_line == NULL) return NULL;

    char *token;
    char *saveptr;
    *count = 0;
    
    token = strtok_r(temp_line, " ", &saveptr);
    while (token != NULL) {
        if (token[0] != '\0') (*count)++;
        token = strtok_r(NULL, " ", &saveptr);
    }
    
    if (*count == 0) {
        free(temp_line);
        return NULL;
    }

    char **args = (char **)malloc((*count + 1) * sizeof(char *));
    if (args == NULL) {
        free(temp_line);
        return NULL;
    }

    int i = 0;
    token = strtok_r(line, " ", &saveptr);
    while (token != NULL) {
        if (token[0] != '\0') {
            args[i++] = token;
        }
        token = strtok_r(NULL, " ", &saveptr);
    }

    args[*count] = NULL; 
    free(temp_line);
    return args;
}

int main(){
    char line[2 * PATH_MAX + 50]; 

    workerList* workers;
    init_workerList(&workers);

    while (1) {
        printf("SyncShell> ");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }
        line[strcspn(line, "\n")] = 0;


        int argc;
        char** argv = split_line(line, &argc);
        if(argv == NULL || argc == 0)
            continue;

        char* cmd = argv[0];
        if(strcmp(cmd, "add") == 0){
            if(argc < 3)
                continue;
            if(!prep_dirs(argv[1], (argv+2), workers)){
                printf("incorrect src\n");
                continue;
            }
            
            pid_t pid = fork();
            if(pid == 0){
                child_work(argv[1], argv+2);
                exit(EXIT_SUCCESS);
            }
            add_worker(argv[1], argv+2, argc-2, pid, workers);
        }
        else if(strcmp(cmd, "list") == 0){
            display_workerList(workers);
        }
        else if(strcmp(cmd, "exit") == 0){
            break;
        }
        else{
            printf("command unknown\n");
        }
            
    }

    kill(0, SIGTERM);
    return 0;
}