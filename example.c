#include "linenoise.h"

#include <stdio.h>

static char* hist_file = "history.lsp";

int main(void)
{
	char* line = NULL;
	printf("This is a simple linenoise test\n");
	line_history_load(hist_file);
	line_set_vi_mode(1);

	while ((line = line_editor("> "))) {
		printf("\"%s\"\n", line);
	}

	return 0;
}
