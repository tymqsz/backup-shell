#ifndef UH
#define UH

#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "fileproc.h"
#include "worker.h"
#include "utils.h"
#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

void setInfoHandler(void (*)(int, siginfo_t*, void* ), int);

void setHandler(void (*)(int), int);

char** split_line(char *, int *);

#endif