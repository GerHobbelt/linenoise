#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/select.h>
#include <ctype.h>
#include "linenoise.h"

#define POLL_TIMEOUT_MS 10000

bool do_exit = false;

void completion(const char *buf, size_t pos, linenoiseCompletions *lc) {
    (void) pos;
    if (strncmp(buf, "multi kulti", 11) == 0) {
        // No hints
    } else if (strncmp(buf, "multi", 5) == 0 && (buf[5] == '\0' || isspace(buf[5]))) {
        linenoiseAddCompletion(lc, "kulti", "multi kulti", SIZE_MAX);
    } else if (strncmp(buf, "hello", 5) == 0 && (buf[5] == '\0' || isspace(buf[5]))) {
        if (buf[5] == '\0' || (isspace(buf[5]) && (buf[6] == '\0' || buf[6] == 't')))
            linenoiseAddCompletion(lc, "there", "hello there", SIZE_MAX);
        if (buf[5] == '\0' || (isspace(buf[5]) && (buf[6] == '\0' || buf[6] == 'h')))
            linenoiseAddCompletion(lc, "here", "hello here", SIZE_MAX);
    } else {
        if (buf[0] == 'h' || buf[0] == '\0')
            linenoiseAddCompletion(lc, "hello", "hello ", SIZE_MAX);
        if (buf[0] == 'm' || buf[0] == '\0')
            linenoiseAddCompletion(lc, "multi", "multi ", SIZE_MAX);
    }
}

void sigint_handler(int signum)
{
    (void) signum;
    linenoiseCancel();
}

void sigwinch_handler(int signum)
{
    (void) signum;
    linenoiseUpdateSize();
}

void sigalrm_handler(int signum)
{
    (void) signum;
    // This signal just wakes-up the poll method, so that the method linenoise
    // gets called - this differentiates handling of the ESC key from the ANSI
    // escape sequences.
}

void sigterm_handler(int signum)
{
    (void) signum;
    do_exit = true;
}

int main(int argc, char **argv) {
    char *line;
    char *prgname = argv[0];
    bool async = false;

    /* Parse options, with --multiline we enable multi line editing. */
    while(argc > 1) {
        argc--;
        argv++;
        if (!strcmp(*argv,"--multiline")) {
            linenoiseSetMultiLine(1);
            printf("Multi-line mode enabled.\n");
        } else  if (!strcmp(*argv,"--async")) {
            async = true;

            int flagsRead = fcntl(STDIN_FILENO, F_GETFL, 0);
            fcntl(STDIN_FILENO, F_SETFL, flagsRead | O_NONBLOCK);

            printf("Asynchronous mode enabled.\n");
        } else {
            fprintf(stderr, "Usage: %s [--multiline] [--async]\n", prgname);
            exit(1);
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);
    sa.sa_handler = sigwinch_handler;
    sigaction(SIGWINCH, &sa, NULL);
    sa.sa_handler = sigalrm_handler;
    sigaction(SIGALRM, &sa, NULL);
    sa.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &sa, NULL);

    /* Set the prompt. */
    linenoiseSetPrompt("hello> ");

    /* Set the completion callback. This will be called every time the
     * user uses the <tab> key. */
    linenoiseSetCompletionCallback(completion);

    /* Load history from file. The history file is just a plain text file
     * where entries are separated by newlines. */
    linenoiseHistoryLoad("history.txt"); /* Load the history at startup */

    /* Now this is the main loop of the typical linenoise-based application.
     * The call to linenoise() will block as long as the user types something
     * and presses enter.
     *
     * The typed string is returned as a malloc() allocated string by
     * linenoise, so the user needs to free() it. */
    int found_error = 0;

    do {
        if (async) {
            // Block signals to have a reliable call to linenoiseHasPendingChar
            sigset_t set, oldset;
            sigemptyset(&set);
            sigaddset(&set, SIGINT);
            sigaddset(&set, SIGALRM);
            sigaddset(&set, SIGWINCH);
            pthread_sigmask(SIG_BLOCK, &set, &oldset);

            linenoiseShowPrompt();

            if (!linenoiseHasPendingChar()) {
                fd_set fds;
                struct timespec tv = { tv_sec: POLL_TIMEOUT_MS / 1000,
                        tv_nsec : (POLL_TIMEOUT_MS % 1000) * 1000000L };
                FD_ZERO(&fds);
                FD_SET(STDIN_FILENO, &fds);
                int selectresult = pselect(STDIN_FILENO + 1, &fds, NULL, NULL, &tv, &oldset);
                if (selectresult == 0) {
                    linenoiseCustomOutput();
                    printf("* Ping\n");
                }
            }

            pthread_sigmask(SIG_SETMASK, &oldset, NULL);
        }

        errno = 0;
        line = linenoise();
        found_error = errno;
        if (line != NULL) {
            /* Do something with the string. */
            if (line[0] != '\0' && line[0] != '/') {
                if (async)  // Can be called also in blocking mode (does nothing)
                    linenoiseCustomOutput();
                printf("echo: '%s'\n", line);
                linenoiseHistoryAdd(line); /* Add to the history. */
                linenoiseHistorySave("history.txt"); /* Save the history on disk. */
            } else if (!strncmp(line,"/historylen",11)) {
                /* The "/historylen" command will change the history len. */
                int len = atoi(line+11);
                linenoiseHistorySetMaxLen(len);
            } else if (line[0] == '/') {
                if (async)  // Can be called also in blocking mode (does nothing)
                    linenoiseCustomOutput();
                printf("Unreconized command: %s\n", line);
            }
            free(line);
        }
    } while ((line != NULL || found_error == EWOULDBLOCK
            || found_error == EAGAIN) && !do_exit);

    linenoiseCleanup();

    if (found_error != 0 && found_error != EINTR) {
        printf("Error: %s\n", strerror(found_error));
    }
    return 0;
}
