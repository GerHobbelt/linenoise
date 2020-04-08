#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/linenoise.h"

/*-----------------------------------------------------------------------------------------*/

#define vt100_reset "\x1b[0m"
#define vt100_bright "\x1b[1m"
#define vt100_dim "\x1b[2m"
#define vt100_underscore "\x1b[4m"
#define vt100_blink "\x1b[5m"
#define vt100_reverse "\x1b[7m"
#define vt100_hidden "\x1b[8m"

#define vt100_black "\x1b[30m"
#define vt100_red "\x1b[31m"
#define vt100_green "\x1b[32m"
#define vt100_yellow "\x1b[33m"
#define vt100_blue "\x1b[34m"
#define vt100_magenta "\x1b[35m"
#define vt100_cyan "\x1b[36m"
#define vt100_white "\x1b[37m"

#define vt100_bgblack "\x1b[40m"
#define vt100_bgred "\x1b[41m"
#define vt100_bggreen "\x1b[42m"
#define vt100_bgyellow "\x1b[43m"
#define vt100_bgblue "\x1b[44m"
#define vt100_bgmagenta "\x1b[45m"
#define vt100_bgcyan "\x1b[46m"
#define vt100_bgwhite "\x1b[47m"

/*-----------------------------------------------------------------------------------------*/
static const char* examples[] = {
    "db",      "hello",        "hallo", "hans",  "hansekogge",
    "seamann", "quetzalcoatl", "quit",  "power", NULL};

void completionHook(char const* prefix, linenoiseCompletions* lc) {
  size_t i;

  for (i = 0; examples[i] != NULL; ++i) {
    if (strncmp(prefix, examples[i], strlen(prefix)) == 0) {
      linenoiseAddCompletion(lc, examples[i]);
    }
  }
}

int main(int argc, char** argv) {

    #ifdef _WIN32
  system("chcp 65001 > NUL");
    #endif

  linenoiseInstallWindowChangeHandler();

  while (argc > 1) {
    argc--;
    argv++;
    if (!strcmp(*argv, "--keycodes")) {
      linenoisePrintKeyCodes();
      exit(0);
    }
  }

  const char* file = "./history";

  linenoiseHistoryLoad(file);
  linenoiseSetCompletionCallback(completionHook);

  printf( ""vt100_green"build:[" __TIMESTAMP__ "] "vt100_reset"exit: ctrl+D ...\n");

  char const* prompt =  vt100_cyan"linenoise"vt100_reset"> ";

  while (1) {
    char* result = linenoise(prompt);

    if (result == NULL) {
      break;
    } else if (!strncmp(result, "/history", 8)) {
      /* Display the current history. */
      for (int index = 0;; ++index) {
        char* hist = linenoiseHistoryLine(index);
        if (hist == NULL) break;
        printf("%4d: %s\n", index, hist);
        free(hist);
      }
    } else if (!strncmp(result, "/?", 2)) {
      /* show the keys */
      linenoisePrintKeyCodes();
    }
    if (*result == '\0') {
      free(result);
      break;
    }

    printf("thanks for the input.\n");
    linenoiseHistoryAdd(result);
    free(result);
  }

  linenoiseHistorySave(file);
  linenoiseHistoryFree();
}
