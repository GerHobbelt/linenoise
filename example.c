#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
// as per https://stackoverflow.com/questions/15826188/what-most-correct-way-to-set-the-encoding-in-c
// see also https://github.com/microsoft/terminal/issues/13680 
#include <windows.h>

#define strcasecmp(a, b) stricmp(a, b)
#endif

#include "linenoise.h"

#if 0		// UTF8 console on Win32 is still b0rked...
#define UTF8
#endif

#include "monolithic_examples.h"

#ifndef NO_COMPLETION

static void completion(const char *buf, linenoiseCompletions *lc, void *userdata) {
    (void)userdata;
    if (buf[0] == 'h') {
#ifdef UTF8
        linenoiseAddCompletion(lc,"hello こんにちは");
        linenoiseAddCompletion(lc,"hello こんにちは there");
        linenoiseAddCompletion(lc,"hello こんにちは 👨‍💻");
#else
        linenoiseAddCompletion(lc,"hello");
        linenoiseAddCompletion(lc,"hello there");
#endif
    }
}

static char *hints(const char *buf, int *color, int *bold, void *userdata) {
    (void)userdata;
    if (!strcasecmp(buf,"hello")) {
        *color = 35;
        *bold = 0;
        return " World";
    }
#ifdef UTF8
    if (!strcasecmp(buf, "こんにちは")) {
        *color = 35;
        *bold = 0;
        return " 世界";
    }
#endif
		return NULL;
}

#endif


static int in_string = 0;
static size_t string_start = 0;

static void reset_string_mode(void) {
	in_string = 0;
	string_start = 0;
}

static int foundspace(stringbuf *buf, int pos, int c) {
    if (in_string) return 0;

    if (buf->last == 0) return 1;

    if (buf->data[buf->last -1] == c) return 1;

    printf("\r\nSPACE!\r\n");
    return 0;
}

static int escapedquote(const char *start)
{
    while (*start) {
        if (*start == '\\') {
	    if (!start[1]) return 1;
	    start += 2;
	}
	start++;
    }
    return 0;
}


static int foundquote(stringbuf *buf, int pos, int c) {
    if (!in_string) {
        in_string = 1;
        string_start = buf->last;
        return 0;
    }

    if (buf->data[string_start] != c) return 0;

    if (escapedquote(buf->data + string_start)) return 0;

    in_string = 0;
    printf("\r\nSTRING %s%c\r\n", buf->data + string_start, buf->data[string_start]);
    string_start = 0;

    return 0;
}

static int foundhelp(stringbuf *buf, int pos, int c) {
    if (in_string) return 0;

    printf("?\r\nHELP: %s\r\n", buf->data);
    return 1;
}


#if defined(BUILD_MONOLITHIC)
#define main      linenoise_example_main
#endif

int main(int argc, const char **argv) {
#ifdef UTF8
    const char* prompt = "hello😊Яあ🐈> ";
#else
    const char* prompt = "hello> ";
#endif
	char* line;
    const char *prgname = argv[0];
	const char *initial;

	// SetConsoleModeToUTF8:
#if defined(_WIN32) || defined(_WIN64)
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif

    /* Parse options, with --multiline we enable multi line editing. */
    while(argc > 1 && argv[1][0] == '-') {
        argc--;
        argv++;
        if (!strcmp(*argv,"--multiline")) {
            linenoiseSetMultiLine(1);
            printf("Multi-line mode enabled.\n");
        } else if (!strcmp(*argv,"--keycodes")) {
            linenoisePrintKeyCodes();
            return 0;
        } else if (!strcmp(*argv,"--fancyprompt")) {
            prompt = "\x1b[1;31m\xf0\xa0\x8a\x9d-\xc2\xb5hello>\x1b[0m ";
        } else if (!strcmp(*argv,"--prompt") && argc > 1) {
            argc--;
            argv++;
            prompt = *argv;
        } else {
            fprintf(stderr, "Usage: %s [--multiline] [--keycodes] [--fancyprompt] [--prompt text]\n", prgname);
            exit(1);
        }
    }

#ifndef NO_COMPLETION
    /* Set the completion callback. This will be called every time the
     * user uses the <tab> key. */
    linenoiseSetCompletionCallback(completion, NULL);
    linenoiseSetHintsCallback(hints, NULL);
#endif

    /* Load history from file. The history file is just a plain text file
     * where entries are separated by newlines. */
    linenoiseHistoryLoad("history.txt"); /* Load the history at startup */
    linenoiseSetCharacterCallback(foundspace, ' ');
    linenoiseSetCharacterCallback(foundquote, '"');
    linenoiseSetCharacterCallback(foundquote, '\'');
    linenoiseSetCharacterCallback(foundhelp, '?');

	initial = (argc > 1) ? argv[1] : "";

    /* Now this is the main loop of the typical linenoise-based application.
     * The call to linenoise() will block as long as the user types something
     * and presses enter.
     *
     * The typed string is returned as a malloc() allocated string by
     * linenoise, so the user needs to free() it. */
    while((line = linenoiseWithInitial(prompt, initial)) != NULL) {
		initial = "";
		reset_string_mode();
        /* Do something with the string. */
        if (line[0] != '\0' && line[0] != '/') {
            printf("echo: '%s'\n", line);
            linenoiseHistoryAdd(line); /* Add to the history. */
            linenoiseHistorySave("history.txt"); /* Save the history on disk. */
        } else if (!strncmp(line,"/historylen",11)) {
            /* The "/historylen" command will change the history len. */
            int len = atoi(line+11);
            linenoiseHistorySetMaxLen(len);
        } else if (!strncmp(line, "/mask", 5)) {
            //linenoiseMaskModeEnable();
        } else if (!strncmp(line, "/unmask", 7)) {
            //linenoiseMaskModeDisable();
        } else if (line[0] == '/') {
            printf("Unrecognized command: %s\n", line);
        }
        free(line);
    }
    return 0;
}
