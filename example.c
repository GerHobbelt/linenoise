#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/linenoise.h"


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

  printf("" vt100_green
         "build:[%s]"vt100_reset" ctrl+D | /? | /history\n",
         __TIMESTAMP__);

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
