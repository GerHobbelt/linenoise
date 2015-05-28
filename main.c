#include "libline.h"
#include <stdio.h>

static char *hist_file = "history.lsp";

int main(void)
{
        char *line = NULL;
        printf("This is a simple linenoise test\n");
        linenoise_history_load(hist_file);
        linenoise_vi_mode(1);

        while((line = linenoise("> "))) {
                printf("\"%s\"\n", line);
        }

        return 0;
}
