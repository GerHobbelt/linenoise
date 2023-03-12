#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fortify.h"

#include "linenoise.h"

#ifdef __riscos
#define EXTSEP "/"
#else
#define EXTSEP "."
#endif

#define HISTORY_FILENAME "history" EXTSEP "txt"

void completion(const char *buf, linenoiseCompletions *lc) {
    if (buf[0] == 'h') {
        linenoiseAddCompletion(lc,"hello");
        linenoiseAddCompletion(lc,"hello there");
    }
}

char *hints(const char *buf, int *color, int *bold) {
    /*
#ifdef __riscos
    if (!strcmp(buf,"hello")) {
#else
    if (!strcasecmp(buf,"hello")) {
#endif
        *color = 35;
        *bold = 0;
        return " World";
    }
    */
    return NULL;
}

int insert_numbers_only(char c, const char *buffer, int pos)
{
    return (c >= '0' && c <= '9');
}

int main(int argc, char **argv) {
    char *line;
    char *prgname = argv[0];

    Fortify_EnterScope();

    /* Parse options, with --multiline we enable multi line editing. */
    while(argc > 1) {
        argc--;
        argv++;
        if (!strcmp(*argv,"--multiline")) {
            linenoiseSetMultiLine(1);
            printf("Multi-line mode enabled.\n");
        } else if (!strcmp(*argv,"--keycodes")) {
            linenoisePrintKeyCodes();
            exit(0);
        } else {
            fprintf(stderr, "Usage: %s [--multiline] [--keycodes]\n", prgname);
            exit(1);
        }
    }

    /* Set the completion callback. This will be called every time the
     * user uses the <tab> key. */
    linenoiseSetCompletionCallback(completion);
    linenoiseSetHintsCallback(hints);

    /* Load history from file. The history file is just a plain text file
     * where entries are separated by newlines. */
    linenoiseHistoryLoad(HISTORY_FILENAME); /* Load the history at startup */

    /* Now this is the main loop of the typical linenoise-based application.
     * The call to linenoise() will block as long as the user types something
     * and presses enter.
     *
     * The typed string is returned as a malloc() allocated string by
     * linenoise, so the user needs to free() it. */

    while((line = linenoise("hello> ")) != NULL) {
        /* Do something with the string. */
        if (line[0] != '\0' && line[0] != '/') {
            printf("echo: '%s'\n", line);
            linenoiseHistoryAdd(line); /* Add to the history. */
            linenoiseHistorySave(HISTORY_FILENAME); /* Save the history on disk. */
        } else if (!strncmp(line,"/historylen",11)) {
            /* The "/historylen" command will change the history len. */
            int len = atoi(line+11);
            linenoiseHistorySetMaxLen(len);
        } else if (!strncmp(line,"/linelen",8)) {
            /* The "/linelen" command will change the max line len. */
            int len = atoi(line+8);
            linenoiseSetMaxLen(len);
        } else if (!strncmp(line, "/mask", 5) && (line[5] == ' ' || line[5] == '\0')) {
            if (line[5] == ' ')
                linenoiseMaskModeChar(line[6]);
            else
                linenoiseMaskModeEnable();
        } else if (!strncmp(line, "/unmask", 7)) {
            linenoiseMaskModeDisable();
        } else if (!strncmp(line, "/numbers", 8)) {
            linenoiseSetInsertCallback(insert_numbers_only);
        } else if (!strncmp(line, "/history", 8)) {
            /* List the history */
            int index;
            for (index=0; ;index ++)
            {
                const char *line = linenoiseHistoryGetLine(index);
                if (!line)
                    break;
                printf("History#%i: %s\n", index, line);
            }
        } else if (!strncmp(line, "/clear", 6)) {
            linenoiseHistoryClear();
        } else if (line[0] == '/') {
            printf("Unreconized command: %s\n", line);
        }
        linenoiseFree(line);
    }

    linenoiseShutdown();

    Fortify_LeaveScope();
    return 0;
}
