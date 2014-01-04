#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include "linenoise.h"

#define POLL_TIMEOUT_MS 10000

void completion(const char *buf, size_t pos, linenoiseCompletions *lc) {
    (void) pos;
    if (buf[0] == 'h' || buf[0] == '\0') {
        linenoiseAddCompletion(lc, "hello", SIZE_MAX);
        linenoiseAddCompletion(lc, "hello there", SIZE_MAX);
    }
    if (buf[0] == 'm' || buf[0] == '\0') {
        linenoiseAddCompletion(lc, "multi", SIZE_MAX);
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

    signal(SIGINT, sigint_handler);
    signal(SIGWINCH, sigwinch_handler);

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
            linenoiseShowPrompt("hello> ");

            int pollresult;
            struct pollfd fds[1] = {{STDIN_FILENO, POLLIN, 0}};
            pollresult = poll(fds, 1, POLL_TIMEOUT_MS);
            if (pollresult == 0) {
                linenoiseCustomOutput();
                printf("* Ping\n");
            }
        }

        errno = 0;
        line = linenoise("hello> ");
        found_error = errno;
        if (line != NULL) {
            /* Do something with the string. */
            if (line[0] != '\0' && line[0] != '/') {
                linenoiseCustomOutput();    // Needed only in async mode
                printf("echo: '%s'\n", line);
                linenoiseHistoryAdd(line); /* Add to the history. */
                linenoiseHistorySave("history.txt"); /* Save the history on disk. */
            } else if (!strncmp(line,"/historylen",11)) {
                /* The "/historylen" command will change the history len. */
                int len = atoi(line+11);
                linenoiseHistorySetMaxLen(len);
            } else if (line[0] == '/') {
                linenoiseCustomOutput();    // Needed only in async mode
                printf("Unreconized command: %s\n", line);
            }
            free(line);
        }
    } while (line != NULL || (found_error == EWOULDBLOCK || found_error == EAGAIN));

    return 0;
}
