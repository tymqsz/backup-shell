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
#include <sys/wait.h>

#include "fileproc.h"
#include "synchro.h"
#include "worker.h"

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))


workerList* workers;

void handleChildDeath(int sig, siginfo_t* info, void* v){
    //waitpid(info->si_pid, NULL, WNOHANG);

    delete_workers_by_pid(info->si_pid, workers);
}

void setHandler(void (*f)(int, siginfo_t*, void* ), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_sigaction = f;
    act.sa_flags = SA_RESTART | SA_SIGINFO; 

    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

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
    setHandler(handleChildDeath, SIGUSR1);
    char line[2 * PATH_MAX + 50]; 

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
            if(argc < 3){
                printf("Usage: add <source> <dest1> [dest2] ...\n");
                free(argv); // Don't forget to free
                continue;
            }

            char* source = argv[1];

            // Loop through every destination argument provided
            for(int i = 2; i < argc; i++) {
                char* current_dest = argv[i];

                // Validate this specific source->dest pair
                if(!prep_dirs(source, current_dest, workers)){
                    printf("Skipping invalid target: %s\n", current_dest);
                    continue;
                }
                
                pid_t pid = fork();
                if (pid < 0) {
                    perror("fork");
                } 
                else if(pid == 0){
                    child_work(source, current_dest);
                    exit(EXIT_SUCCESS);
                }
                
                add_worker(source, current_dest, pid, workers);
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
            mirror_restore_recursive(argv[2], argv[1]);
        }
        else{
            printf("command unknown\n");
        }
        
        free(argv); 
    }

    // Clean up
    kill(0, SIGTERM);
    return 0;
}