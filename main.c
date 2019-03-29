#include "libline.h"
#include <stdio.h>

static char *hist_file = "history.txt";

int main(void) {
        char *line = NULL;
        printf("This is a simple linenoise test\n");
        line_history_load(hist_file);
        line_set_vi_mode(1);

        while((line = line_editor("> "))) {
		line_history_add(line);
                printf("\"%s\"\n", line);
	}

	return line_history_save(hist_file);
}
