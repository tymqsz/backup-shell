#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"
#include "fileproc.h"
#include "worker.h"
#include "utils.h"



void setInfoHandler(void (*f)(int, siginfo_t*, void* ), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_sigaction = f;
    act.sa_flags = SA_RESTART | SA_SIGINFO; 

    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

void setHandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;

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