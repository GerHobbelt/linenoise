#include "libline.h"
#include <stdio.h>
#include <stdlib.h>

static char *hist_file = "history.txt";
static libline_t ll = { 0 };

int main(void) {
        printf("This is a simple linenoise test\n");
	line_initialize(&ll);
        line_history_load(&ll, hist_file);
        line_set_vi_mode(&ll, 1);

        for(char *line = NULL; (line = line_editor(&ll, "> ")); free(line)) {
		line_history_add(&ll, line);
                printf("\"%s\"\n", line);
	}

	line_history_save(&ll, hist_file);
	line_cleanup(&ll);
	return 0;
}
