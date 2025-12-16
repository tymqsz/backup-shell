#define _GNU_SOURCE

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "fileproc.h"
#include "synchro.h"
#include "utils.h"
#include "worker.h"

volatile sig_atomic_t END = 0;

void handleInterruption(int sig)
{
    /* end of app */
    END = 1;
}

void collectDeadWorkers(workerList *workers)
{
    pid_t pid;
    int worker_cnt = workers->size, i = 0;

    while (i < worker_cnt)
    {
        pid = waitpid(-1, NULL, WNOHANG);
        if (pid <= 0)
            return;

        delete_workers_by_pid(pid, workers);
        i++;
    }
}

int main()
{
    sigset_t mask;
    sigfillset(&mask);
    sigdelset(&mask, SIGINT);
    sigdelset(&mask, SIGTERM);
    sigprocmask(SIG_SETMASK, &mask, NULL);

    setHandler(handleInterruption, SIGINT);
    setHandler(handleInterruption, SIGTERM);

    workerList *workers;
    init_workerList(&workers);
    BackupList *b_list = initBackupList();

    char line[10 * PATH_MAX];
    while (!END)
    {
        printf("command: ");
        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            break;
        }
        line[strcspn(line, "\n")] = 0;

        int argc;
        char **argv = split_line(line, &argc);
        if (argv == NULL || argc == 0)
            continue;

        char *cmd = argv[0];

        /* check if any of workers died */
        collectDeadWorkers(workers);

        if (strcmp(cmd, "add") == 0)
        {
            time_t t = time(NULL);
            if (argc < 3)
            {
                printf("usage: add <source path> <target paths>\n");
                free(argv);
                continue;
            }

            for (int i = 2; i < argc; i++)
            {
                if (prep_dirs(argv[1], argv[i], workers) == -1)
                {
                    printf("invalid arguments.\n");
                    break;
                }

                pid_t pid = fork();
                if (pid < 0)
                {
                    ERR("fork");
                }
                else if (pid == 0)
                {
                    setHandler(SIG_DFL, SIGTERM);
                    backup_work(argv[1], argv[i]);
                    exit(EXIT_SUCCESS);
                }

                insertBackupNode(b_list, argv[i], t);
                add_worker(argv[1], argv[i], pid, workers);
            }
        }
        else if (strcmp(cmd, "end") == 0)
        {
            if (argc < 3)
            {
                printf("usage: end <source path> <target paths>.\n");
                free(argv);
                continue;
            }

            if (delete_workers_by_paths(argv[1], argv + 2, workers) == -1)
            {
                printf("invalid arguments.\n");
            }
        }
        else if (strcmp(cmd, "list") == 0)
        {
            display_workerList(workers);
        }
        else if (strcmp(cmd, "exit") == 0)
        {
            free(argv);
            break;
        }
        else if (strcmp(cmd, "restore") == 0)
        {
            if (argc != 3)
            {
                printf("usage: restore <source path> <target path>.\n");
                free(argv);
                continue;
            }
            time_t t = getTimeFromDst(b_list, argv[2]);
            restore(argv[1], argv[2], t);
        }
        else
        {
            printf("command unknown.\n");
        }

        free(argv);
    }

    freeBackupList(b_list);
    delete_all_workers(workers);
    kill(0, SIGTERM);
    return 0;
}
