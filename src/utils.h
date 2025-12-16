#ifndef UH
#define UH

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "fileproc.h"
#include "utils.h"
#include "worker.h"
#define NULL_TIME ((time_t)-1)
#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

typedef struct BackupNode
{
    char* dst;
    time_t c_time;
    struct BackupNode* next;
} BackupNode;

typedef struct BackupList
{
    BackupNode* head;
    int size;
} BackupList;

void setInfoHandler(void (*)(int, siginfo_t*, void*), int);

void setHandler(void (*)(int), int);

char** split_line(char*, int*);

BackupList* initBackupList();

int insertBackupNode(BackupList* list, const char* destination, time_t initial_time);
void freeBackupList(BackupList* list);

time_t getTimeFromDst(const BackupList* list, const char* target_dst);

#endif
