#define _GNU_SOURCE

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fileproc.h"
#include "utils.h"
#include "worker.h"

void setInfoHandler(void (*f)(int, siginfo_t*, void*), int sigNo)
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

char** split_line(char* line, int* count)
{
    if (line[0] != '\0' && line[strlen(line) - 1] == '\n')
    {
        line[strlen(line) - 1] = '\0';
    }

    char* temp_line = strdup(line);
    if (temp_line == NULL)
        return NULL;

    char* token;
    char* saveptr;
    *count = 0;

    token = strtok_r(temp_line, " ", &saveptr);
    while (token != NULL)
    {
        if (token[0] != '\0')
            (*count)++;
        token = strtok_r(NULL, " ", &saveptr);
    }

    if (*count == 0)
    {
        free(temp_line);
        return NULL;
    }

    char** args = (char**)malloc((*count + 1) * sizeof(char*));
    if (args == NULL)
    {
        free(temp_line);
        return NULL;
    }

    int i = 0;
    token = strtok_r(line, " ", &saveptr);
    while (token != NULL)
    {
        if (token[0] != '\0')
        {
            args[i++] = token;
        }
        token = strtok_r(NULL, " ", &saveptr);
    }

    args[*count] = NULL;
    free(temp_line);
    return args;
}

BackupList* initBackupList()
{
    BackupList* list = (BackupList*)malloc(sizeof(BackupList));
    if (list == NULL)
        return NULL;
    list->head = NULL;
    list->size = 0;
    return list;
}

int insertBackupNode(BackupList* list, const char* destination, time_t initial_time)
{
    if (list == NULL)
        return -1;

    BackupNode* newNode = (BackupNode*)malloc(sizeof(BackupNode));
    if (newNode == NULL)
        return -1;

    newNode->dst = (char*)malloc(strlen(destination) + 1);
    if (newNode->dst == NULL)
    {
        free(newNode);
        return -1;
    }
    strcpy(newNode->dst, destination);

    newNode->c_time = initial_time;
    newNode->next = list->head;

    list->head = newNode;
    list->size++;

    return 0;
}

void freeBackupList(BackupList* list)
{
    if (list == NULL)
        return;

    BackupNode* current = list->head;
    BackupNode* next_node;

    while (current != NULL)
    {
        next_node = current->next;

        if (current->dst != NULL)
        {
            free(current->dst);
        }

        free(current);

        current = next_node;
    }

    free(list);
}

time_t getTimeFromDst(const BackupList* list, const char* target_dst)
{
    if (list == NULL || list->head == NULL)
    {
        return NULL_TIME;
    }

    BackupNode* current = list->head;

    while (current != NULL)
    {
        if (current->dst != NULL && strcmp(current->dst, target_dst) == 0)
        {
            return current->c_time;
        }
        current = current->next;
    }

    return NULL_TIME;
}
