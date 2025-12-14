#ifndef UH
#define UH

#include <signal.h>
#include <fcntl.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

void setInfoHandler(void (*f)(int, siginfo_t*, void* ), int sigNo);

void setHandler(void (*f)(int), int sigNo);

char** split_line(char *line, int *count);

#endif