/**@file    libline.c 
 * @brief   A guerrilla line editing library against the idea that a
 *          line editing lib needs to be 20,000 lines of C code.
 * @author  Salvatore Sanfilippo
 * @author  Pieter Noordhuis
 * @author  Richard James Howe
 * @license BSD (included as comment)
 *
 * * TODO:
 * - Full Windows Port, allow this to be ported to embedded platforms as well
 * - Assertions
 * - Put system dependent functionality into a series of callbacks, including
 *   the allocator, making this suitable to porting to an embedded environment 
 * - There are a few vi commands that should be added (rR)
 * - The vi section and the rest should be separated.
 *
 * You can find the original/latest source code at:
 * 
 *   http://github.com/antirez/linenoise
 *
 * Does a number of crazy assumptions that happen to be true in 99.9999% of
 * the 2010 UNIX computers around.
 * 
 * DEVIATIONS FROM ORIGINAL:
 * 
 * This merges and updates changes from user 'bobrippling' on Github.com
 * <https://github.com/bobrippling/linenoise/blob/master/linenoise.c>
 * <https://github.com/antirez/linenoise/pull/11>
 *
 * The API has also been changed so better suite my style and a few minor
 * typos fixed.
 *
 * ------------------------------------------------------------------------
 *
 * ADDITIONAL COPYRIGHT
 *
 * Copyright (c) 2015, Richard James Howe <howe.r.j.89@gmail.com>
 *
 * ORIGINAL COPYRIGHT HEADER
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2013, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * ------------------------------------------------------------------------
 *
 * References:
 * - http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * - http://www.3waylabs.com/nw/WWW/products/wizcon/vt220.html
 *
 * List of escape sequences used by this program, we do everything just
 * with three sequences. In order to be so cheap we may have some
 * flickering effect with some slow terminal, but the lesser sequences
 * the more compatible.
 *
 * CHA (Cursor Horizontal Absolute)
 *    Sequence: ESC [ n G
 *    Effect: moves cursor to column n
 *
 * EL (Erase Line)
 *    Sequence: ESC [ n K
 *    Effect: if n is 0 or missing, clear from cursor to end of line
 *    Effect: if n is 1, clear from beginning of line to cursor
 *    Effect: if n is 2, clear entire line
 *
 * CUF (CUrsor Forward)
 *    Sequence: ESC [ n C
 *    Effect: moves cursor forward of n chars
 *
 * When multi line mode is enabled, we also use an additional escape
 * sequence. However multi line editing is disabled by default.
 *
 * CUU (Cursor Up)
 *    Sequence: ESC [ n A
 *    Effect: moves cursor up of n chars.
 *
 * CUD (Cursor Down)
 *    Sequence: ESC [ n B
 *    Effect: moves cursor down of n chars.
 *
 * The following are used to clear the screen: ESC [ H ESC [ 2 J
 * This is actually composed of two sequences:
 *
 * cursorhome
 *    Sequence: ESC [ H
 *    Effect: moves the cursor to upper left corner
 *
 * ED2 (Clear entire screen)
 *    Sequence: ESC [ 2 J
 *    Effect: clear the whole screen
 * 
 * Useful links for porting to Windows:
 * <https://stackoverflow.com/questions/24708700/c-detect-when-user-presses-arrow-key>
 * <https://msdn.microsoft.com/en-us/library/windows/desktop/ms683462%28v=vs.85%29.aspx>
 * <https://msdn.microsoft.com/en-us/library/windows/desktop/ms686033%28v=vs.85%29.aspx>
 * <https://msdn.microsoft.com/en-us/library/windows/desktop/ms683231%28v=vs.85%29.aspx> */

#include "libline.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifdef __unix__
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#else
#error "Unsupported system"
#endif

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN (128)
#define LINENOISE_MAX_LINE                (512)
#define LINENOISE_HISTORY_NEXT            (0)
#define LINENOISE_HISTORY_PREV            (1)
#define SEQ_BUF_LEN                       (64)

static char *unsupported_term[] = { "dumb", "cons25", "emacs", NULL };

typedef long long le_ssize_t;

struct line_completions {
	size_t len;
	char **cvec;
};

/* The line_state structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct line_state {
	char *buf;          /* Edited line buffer. */
	size_t buflen;      /* Edited line buffer size. */
	const char *prompt; /* Prompt to display. */
	size_t plen;        /* Prompt length. */
	size_t pos;         /* Current cursor position. */
	size_t oldpos;      /* Previous refresh cursor position. */
	size_t len;         /* Current edited line length. */
	size_t cols;        /* Number of columns in terminal. */
	size_t maxrows;     /* Maximum num of rows used so far (multiline mode) */
	int history_index;  /* The history index we are currently editing. */
};

enum KEY_ACTION {
	KEY_NULL  = 0,   /* NULL */
	CTRL_A    = 1,   /* Ctrl+a */
	CTRL_B    = 2,   /* Ctrl-b */
	CTRL_C    = 3,   /* Ctrl-c */
	CTRL_D    = 4,   /* Ctrl-d */
	CTRL_E    = 5,   /* Ctrl-e */
	CTRL_F    = 6,   /* Ctrl-f */
	CTRL_H    = 8,   /* Ctrl-h */
	TAB       = 9,   /* Tab */
	CTRL_K    = 11,  /* Ctrl+k */
	CTRL_L    = 12,  /* Ctrl+l */
	ENTER     = 13,  /* Enter */
	CTRL_N    = 14,  /* Ctrl-n */
	CTRL_P    = 16,  /* Ctrl-p */
	CTRL_T    = 20,  /* Ctrl-t */
	CTRL_U    = 21,  /* Ctrl+u */
	CTRL_W    = 23,  /* Ctrl+w */
	ESC       = 27,  /* Escape */
	BACKSPACE = 127, /* Backspace */
};

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf {
	size_t len;
	char *b;
};

/**TODO Reorder code to avoid forward declarations */
static int line_edit_delete_char(libline_t *ll, struct line_state *l);
static int refresh_line(libline_t *ll, struct line_state *l);

static void *le_realloc(libline_t *ll, void *p, size_t sz) {
	assert(ll);
	return realloc(p, sz);
}

static void le_free(libline_t *ll, void *p) {
	assert(ll);
	free(p);
}

static void *le_malloc(libline_t *ll, size_t sz) {
	assert(ll);
	return malloc(sz);
}

static le_ssize_t le_write(libline_t *ll, void const * buf, size_t count) {
	assert(ll);
	return write(ll->out, buf, count);
}

static char *le_strdup(libline_t *ll, const char *s) { 
	assert(ll);
	assert(s);
	char *str = le_malloc(ll, strlen(s) + 1);
	if (!str) {
		fputs("Out of memory", stderr);
		exit(1); /*not the best thing to do in a library*/
	}
	strcpy(str, s);
	return str;
}

static int le_isatty(libline_t *ll, int fd) {
	assert(ll);
	return isatty(fd);
}

static int disable_raw_mode(libline_t *ll, struct termios *original) {
	assert(ll);
	if (ll->rawmode && tcsetattr(ll->in, TCSANOW, original) != -1)
		ll->rawmode = 0;
	return 0;
}

static int enable_raw_mode(libline_t *ll, struct termios *original) {
	assert(ll);
	struct termios raw;

	if (!le_isatty(ll, ll->in))
		goto error;
	if (tcgetattr(ll->in, original) == -1)
		goto error;

	raw = *original;     /* modify the original mode */
	/* input modes: no break, no CR to NL, no parity check, no strip char,
	no start/stop output control. */
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	/* output modes - disable post processing */
	raw.c_oflag &= ~(OPOST);
	/* control modes - set 8 bit chars */
	raw.c_cflag |= (CS8);
	/* local modes - echoing off, canonical off, no extended functions,
	no signal chars (^Z,^C) */
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	/* control chars - set return condition: min number of bytes and timer.
	We want read to return every single byte, without timeout. */
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;    /* 1 byte, no timer */

	/* put terminal in raw mode now */
	if (tcsetattr(ll->in, TCSANOW, &raw) < 0)
		goto error;
	ll->rawmode = 1;
	return 0;
 error:
	/* errno = ENOTTY; */
	return -1;
}

static le_ssize_t le_read(libline_t *ll, void *buf, size_t count) {
	assert(ll);
	struct termios original;
	const int tty = le_isatty(ll, ll->in);
	if (tty)
		enable_raw_mode(ll, &original);
	const le_ssize_t r = read(ll->in, buf, count);
	if (tty)
		disable_raw_mode(ll, &original);
	return r;
}

static int le_strcasecmp(const char *a, const char *b) {
	assert(a);
	assert(b);
	int ca = 0, cb = 0;
	for (ca = 0, cb = 0; (ca = *a++) && (cb = *b++);) {
		ca = tolower(ca);
		cb = tolower(cb);
		if (ca != cb)
			break;
	}
	return ca - cb;
}

/** @brief Return true if the terminal name is in the list of terminals 
 *  we know are not able to understand basic escape sequences. **/
static int le_is_unsupported_term(void) {
	char *term = getenv("TERM");
	if (!term)
		return 0;
	for (int j = 0; unsupported_term[j]; j++)
		if (!le_strcasecmp(term, unsupported_term[j]))
			return 1;
	return 0;
}

static char *le_fgets(libline_t *ll) {
	assert(ll);
	char *line = NULL;
	size_t len = 0, maxlen = 0;

	while(1) {
		if (len == maxlen) {
			if (maxlen == 0) maxlen = 16;
			maxlen *= 2;
			char *oldval = line;
			line = le_realloc(ll, line, maxlen);
			if (!line) {
				if (oldval) 
					le_free(ll, oldval);
				return NULL;
			}
		}
		const int c = fgetc(stdin);
		if (c == EOF || c == '\n') {
			if (c == EOF && len == 0) {
				le_free(ll, line);
				return NULL;
			} else {
				line[len] = '\0';
				return line;
			}
		} else {
			line[len] = c;
			len++;
		}
	}
}

/************************* Low level terminal handling ***********************/
/**@brief Use the ESC [6n escape sequence to query the horizontal 
   cursor position and return it. On error -1 is returned, 
   on success the position of the cursor. **/
static int get_cursor_position(libline_t *ll) {
	/* Report cursor location */
	if (le_write(ll, "\x1b[6n", 4) != 4)
		return -1;

	/* Read the response: ESC [ rows ; cols R */
	size_t i = 0;
	char buf[32] = { 0 };
	while (i < sizeof(buf) - 1) {
		if (le_read(ll, buf + i, 1) != 1)
			break;
		if (buf[i] == 'R')
			break;
		i++;
	}
	buf[i] = '\0';

	/* Parse it. */
	if (buf[0] != ESC || buf[1] != '[')
		return -1;
	int cols = 0, rows = 0;
	if (sscanf(buf + 2, "%d;%d", &rows, &cols) != 2)
		return -1;
	return cols;
}

/** @brief Try to get the number of columns in the current terminal, or 
 * assume 80 if it fails. **/
static int get_columns(libline_t *ll) {
	assert(ll);
	struct winsize ws;

	if (ioctl(1, TIOCGWINSZ, &ws) != -1 && ws.ws_col != 0)
		return (int)ws.ws_col;
	/* ioctl() failed. Try to query the terminal itself. */

	/* Get the initial position so we can restore it later. */
	const int start = get_cursor_position(ll);
	if (start == -1)
		goto failed;

	/* Go to right margin and get position. */
	if (le_write(ll, "\x1b[999C", 6) != 6)
		goto failed;
	const int cols = get_cursor_position(ll);
	if (cols == -1)
		goto failed;

	/* Restore position. */
	if (cols > start) {
		char seq[32] = { 0 };
		snprintf(seq, sizeof seq, "\x1b[%dD", cols - start);
		if (le_write(ll, seq, strlen(seq)) == -1) {
			goto failed; /* Can't recover... */
		}
	}
	return cols;
 failed:
	return 80;
}

/**@brief Clear the screen. Used to handle ctrl+l **/
int line_clearscreen(libline_t *ll) {
	assert(ll);
	if (le_write(ll, "\x1b[H\x1b[2J", 7) <= 0)
		return -1;
	return 0;
}

/**@brief Beep, used for completion when there is nothing to complete 
 * or when all the choices were already shown. **/
static int line_beep(libline_t *ll) {
	assert(ll);
	return le_write(ll, "\x7", 1);
}

/******************************** Completion *********************************/

/**@brief Free a list of completion options populated by 
 line_add_completion(). **/
static void free_completions(libline_t *ll, line_completions * lc) {
	assert(ll);
	assert(lc);
	for (size_t i = 0; i < lc->len; i++)
		le_free(ll, lc->cvec[i]);
	if (lc->cvec)
		le_free(ll, lc->cvec);
}

/**@brief This is an helper function for line_edit() and is called when the
   user types the <tab> key in order to complete the string currently 
   in the input. The state of the editing is encapsulated into the pointed 
   line_state structure as described in the structure definition. **/
static int complete_line(libline_t *ll, struct line_state *ls) {
	assert(ll);
	assert(ls);
	assert(ll->completion_callback);
	line_completions lc = { 0, NULL };
	size_t stop = 0, i = 0;
	int c = 0;

	ll->completion_callback(ls->buf, ls->pos, &lc);
	if (lc.len == 0) {
		line_beep(ll);
		goto end;
	}
	while (!stop) {
		/* Show completion or original buffer */
		if (i < lc.len) {
			struct line_state saved = *ls;
			ls->len = ls->pos = strlen(lc.cvec[i]);
			ls->buf = lc.cvec[i];
			if (refresh_line(ll, ls) < 0) {
				c = -1;
				goto end;
			}
			ls->len = saved.len;
			ls->pos = saved.pos;
			ls->buf = saved.buf;
		} else {
			if (refresh_line(ll, ls) < 0) {
				c = -1;
				goto end;
			}
		}

		char cb[1] = { 0 };
		const le_ssize_t nread = le_read(ll, &cb, 1);
		c = cb[0];
		if (nread <= 0) {
			c = -1;
			goto end;
		}

		switch (c) {
		case TAB:	/* tab */
			i = (i + 1) % (lc.len + 1);
			if (i == lc.len)
				line_beep(ll);
			break;
		case ESC:       /* escape */
			/* Re-show original buffer */
			if (i < lc.len)
				refresh_line(ll, ls); // !!
			stop = 1;
			break;
		default:
			/* Update buffer and return */
			if (i < lc.len) {
				const int nwritten = snprintf(ls->buf, ls->buflen, "%s", lc.cvec[i]);
				ls->len = ls->pos = (size_t)nwritten;
			}
			stop = 1;
			break;
		}
	}
end:
	free_completions(ll, &lc);
	return c; /* Return last read character, or error */
}

/** @brief Register a callback function to be called for tab-completion. **/
void line_set_completion_callback(libline_t *ll, line_completion_callback * fn) {
	assert(ll);
	ll->completion_callback = fn;
}

/** @brief This function is used by the callback function registered by the user
 *	in order to add completion options given the input string when the
 *	user typed <tab>. See the example.c source code for a very easy to
 *	understand example. **/
void line_add_completion(libline_t *ll, line_completions * lc, const char *str) {
	assert(ll);
	assert(lc);
	assert(str);
	const size_t len = strlen(str);
	char *copy = le_malloc(ll, len + 1);
	if (!copy)
		return;
	memcpy(copy, str, len + 1);
	char **cvec = le_realloc(ll, lc->cvec, sizeof(char *) * (lc->len + 1));
	if (!cvec) {
		le_free(ll, copy);
		return;
	}
	lc->cvec = cvec;
	lc->cvec[lc->len++] = copy;
}

/***************************** Line editing **********************************/

/** @brief Initialize a abuf structure to zero **/
static void ab_init(struct abuf *ab) {
	assert(ab);
	ab->b  = NULL;
	ab->len = 0;
}

/**@brief Append a string to an abuf struct **/
static int ab_append(libline_t *ll, struct abuf *ab, const char *s, size_t len) {
	assert(ab);
	assert(s);
	char *new = le_realloc(ll, ab->b, ab->len + len);
	if (!new)
		return -1;
	memcpy(new + ab->len, s, len);
	ab->b = new;
	ab->len += len;
	return 0;
}

/**@brief Handle abuf memory freeing **/
static void ab_free(libline_t *ll, struct abuf *ab) {
	assert(ab);
	le_free(ll, ab->b);
	ab->b = NULL;
}

/**@brief Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. **/
static int refresh_line(libline_t *ll, struct line_state *l) {
	assert(ll);
	assert(l);
	char seq[SEQ_BUF_LEN] = { 0 };
	size_t plen = strlen(l->prompt);
	char *buf = l->buf;
	size_t len = l->len;
	size_t pos = l->pos;
	struct abuf ab;

	if ((plen + pos) >= l->cols) {
		const size_t take = (plen + pos) - l->cols;
		len -= take;
		pos -= take;
		buf += take;
	}

	if (plen + len > l->cols)
		len = l->cols - plen;

	ab_init(&ab);
	/* Cursor to left edge */
	if (snprintf(seq, SEQ_BUF_LEN, "\x1b[0G") < 0)
		goto fail;
	if (ab_append(ll, &ab, seq, strlen(seq)) < 0)
		goto fail;
	/* Write the prompt and the current buffer content */
	if (ab_append(ll, &ab, l->prompt, strlen(l->prompt)) < 0)
		goto fail;
	if (ab_append(ll, &ab, buf, len) < 0)
		goto fail;
	/* Erase to right */
	snprintf(seq, SEQ_BUF_LEN, "\x1b[0K");
	if (ab_append(ll, &ab, seq, strlen(seq)) < 0)
		goto fail;
	/* Move cursor to original position. */
	if (snprintf(seq, SEQ_BUF_LEN, "\x1b[0G\x1b[%dC", (int)(pos + plen)) < 0)
		goto fail;
	if (ab_append(ll, &ab, seq, strlen(seq)) < 0)
		goto fail;
	if (le_write(ll, ab.b, ab.len) == -1) /* Can't recover from write error. */ 
		goto fail;
	ab_free(ll, &ab);
	return 0;
fail:
	ab_free(ll, &ab);
	return -1;
}

/**@brief Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0.  **/
static int line_edit_insert(libline_t *ll, struct line_state *l, char c) {
	assert(l);
	if (l->len < l->buflen) {
		if (l->len == l->pos) {
			l->buf[l->pos] = c;
			l->pos++;
			l->len++;
			l->buf[l->len] = '\0';
			if ((l->plen + l->len) < l->cols) {
				/* Avoid a full update of the line in the trivial case. */
				if (le_write(ll, &c, 1) == -1)
					return -1;
			} else {
				if (refresh_line(ll, l) < 0)
					return -1;
			}
		} else {
			memmove(l->buf + l->pos + 1, l->buf + l->pos, l->len - l->pos);
			l->buf[l->pos] = c;
			l->len++;
			l->pos++;
			l->buf[l->len] = '\0';
			if (refresh_line(ll, l) < 0)
				return -1;
		}
	}
	return 0;
}

/** @brief Move cursor on the left. **/
static int line_edit_move_left(libline_t *ll, struct line_state *l) {
	assert(ll);
	assert(l);
	if (l->pos > 0) {
		l->pos--;
		if (refresh_line(ll, l) < 0)
			return -1;
	}
	return 0;
}

/** @brief Move cursor on the right. **/
static int line_edit_move_right(libline_t *ll, struct line_state *l) {
	assert(ll);
	assert(l);
	if (l->pos != l->len) {
		l->pos++;
		if (refresh_line(ll, l) < 0)
			return -1;
	}
	return 0;
}

/** @brief Move cursor to the start of the line. **/
static int line_edit_move_home(libline_t *ll, struct line_state *l) {
	assert(ll);
	assert(l);
	if (l->pos != 0) {
		l->pos = 0;
		if (refresh_line(ll, l) < 0)
			return -1;
	}
	return 0;
}

/** @brief Move cursor to the end of the line. **/
static int line_edit_move_end(libline_t *ll, struct line_state *l) {
	assert(ll);
	assert(l);
	if (l->pos != l->len) {
		l->pos = l->len;
		if (refresh_line(ll, l) < 0)
			return -1;
	}
	return 0;
}

/** * @brief Substitute the currently edited line with the next or 
 * previous history entry as specified by 'dir'. **/
static int line_edit_history_next(libline_t *ll, struct line_state *l, int dir) {
	assert(ll);
	assert(l);
	if (ll->history_len > 1) {
		/* Update the current history entry before to
		 * overwrite it with the next one. */
		le_free(ll, ll->history[ll->history_len - 1 - l->history_index]);
		ll->history[ll->history_len - 1 - l->history_index] = le_strdup(ll, l->buf);
		/* Show the new entry */
		l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
		if (l->history_index < 0) {
			l->history_index = 0;
			return 0;
		} else if (l->history_index >= ll->history_len) {
			l->history_index = ll->history_len - 1;
			return 0;
		}
		strncpy(l->buf, ll->history[ll->history_len - 1 - l->history_index], l->buflen);
		l->buf[l->buflen - 1] = '\0';
		l->len = l->pos = strlen(l->buf);
		if (refresh_line(ll, l) < 0)
			return -1;
	}
	return 0;
}

/** @brief Delete the character at the right of the cursor without altering the 
 cursor position. Basically this is what happens with the "Delete" 
 keyboard key. **/
static int line_edit_delete(libline_t *ll, struct line_state *l) {
	assert(ll);
	assert(l);
	if (l->len > 0 && l->pos < l->len) {
		memmove(l->buf + l->pos, l->buf + l->pos + 1, l->len - l->pos - 1);
		l->len--;
		l->buf[l->len] = '\0';
		if (refresh_line(ll, l) < 0)
			return -1;
	}
	return 0;
}

/** @brief Backspace implementation. **/
static int line_edit_backspace(libline_t *ll, struct line_state *l) {
	assert(ll);
	assert(l);
	if (l->pos > 0 && l->len > 0) {
		memmove(l->buf + l->pos - 1, l->buf + l->pos, l->len - l->pos);
		l->pos--;
		l->len--;
		l->buf[l->len] = '\0';
		if (refresh_line(ll, l) < 0)
			return -1;
	}
	return 0;
}

/**@brief Move the cursor to the next word **/
static void line_edit_next_word(struct line_state *l) {
	assert(l);
	while ((l->pos < l->len) && (' ' == l->buf[l->pos + 1]))
		l->pos++;
	while ((l->pos < l->len) && (' ' != l->buf[l->pos + 1]))
		l->pos++;
	if (l->pos < l->len)
		l->pos++;
}

/**@brief Delete the next word, maintaining the cursor at the start 
 *	of the current word.  **/
static int line_edit_delete_next_word(libline_t *ll, struct line_state *l) {
	assert(ll);
	assert(l);
	const size_t old_pos = l->pos;
	line_edit_next_word(l);
	const size_t diff = l->pos - old_pos;
	memmove(l->buf + old_pos, l->buf + l->pos, l->len - old_pos + 1);
	l->len -= diff;
	l->pos = old_pos;
	return refresh_line(ll, l);
}

/**@brief Move the cursor to the previous word. **/
static void line_edit_prev_word(struct line_state *l) {
	assert(l);
	while ((l->pos > 0) && (l->buf[l->pos - 1] == ' '))
		l->pos--;
	while ((l->pos > 0) && (l->buf[l->pos - 1] != ' '))
		l->pos--;
}

/**@brief Delete the previous word, maintaining the cursor at the start 
  of the current word. **/
static int line_edit_delete_prev_word(libline_t *ll, struct line_state *l) {
	assert(ll);
	assert(l);
	size_t old_pos = l->pos;
	line_edit_prev_word(l);
	const size_t diff = old_pos - l->pos;
	memmove(l->buf + l->pos, l->buf + old_pos, l->len - old_pos + 1);
	l->len -= diff;
	return refresh_line(ll, l);
}

/**@brief This function processes vi-key commands. 
 * @param  l    The state of the line editor
 * @param  c    Current keyboard character we are processing
 * @param  buf  Input buffer
 * @return int  -1 on error **/
static int line_edit_process_vi(libline_t *ll, struct line_state *l, char c, char *buf) {
	assert(ll);
	assert(l);
	assert(buf);
	switch (c) {
	case 'x': /*delete char*/
		if (l->pos && (l->pos == l->len))
			l->pos--;
		return line_edit_delete_char(ll, l);
	case 'w': /* move forward a word */
		line_edit_next_word(l);
		return refresh_line(ll, l);
	case 'b': /* move back a word */
		line_edit_prev_word(l);
		return refresh_line(ll, l);
	case 'C': /*Change*/
		ll->vi_escape = 0;
		/*fall through*/
	case 'D': /*Delete from cursor to the end of the line*/
		buf[l->pos] = '\0';
		l->len = l->pos;
		return refresh_line(ll, l);
	case '0': /*Go to the beginning of the line*/
		return line_edit_move_home(ll, l);
	case '$': /*move to the end of the line*/
		return line_edit_move_end(ll, l);
	case 'l': /*move right*/
		return line_edit_move_right(ll, l);
	case 'h': /*move left*/
		return line_edit_move_left(ll, l);
	case 'A':/*append at end of line*/
		l->pos = l->len;
		if (refresh_line(ll, l) < 0)
			return -1;
		/*fall through*/
	case 'a':/*append after the cursor*/
		if (l->pos != l->len) {
			l->pos++;
			if (refresh_line(ll, l) < 0)
				return -1;
		}
		/*fall through*/
	case 'i':/*insert text before the cursor*/
		ll->vi_escape = 0;
		break;
	case 'I':/*Insert text before the first non-blank in the line*/
		ll->vi_escape = 0;
		l->pos = 0;
		return refresh_line(ll, l);
	case 'k': /*move up*/
		return line_edit_history_next(ll, l, LINENOISE_HISTORY_PREV);
	case 'r': { /*replace a character*/
		int replace = 0;
		if (le_read(ll, &replace, 1) == -1)
			return -1;
		buf[l->pos] = replace;
		return refresh_line(ll, l);
	}
	case 'j': /*move down*/
		return line_edit_history_next(ll, l, LINENOISE_HISTORY_NEXT);
	case 'f': /*fall through*/
	case 'F': /*fall through*/
	case 't': /*fall through*/
	case 'T': /*fall through*/
	{
		le_ssize_t dir = 0, lim = 0, cpos = 0;
		int find = 0; 

		if (le_read(ll, &find, 1) == -1) 
			return -1;

		if (islower(c)) {
		    /* forwards */
		    lim = l->len;
		    dir = 1;
		} else {
		    lim = dir = -1;
		}

		for (cpos = l->pos + dir; (cpos < lim) && (cpos > 0); cpos += dir) {
		    if (buf[cpos] == find) {
			l->pos = cpos;
			if (tolower(c) == 't')
			    l->pos -= dir;
			if (refresh_line(ll, l) < 0)
				return -1;
			break;
		    }
		}

		if (cpos == lim) 
			line_beep(ll);
	}
	break;
	case 'c':
		ll->vi_escape = 0;
	case 'd': /*delete*/
	{
		char rc[1] = { 0 };
		if (le_read(ll, rc, 1) == -1)
			return -1;
		switch (rc[0]) {
		case 'w': 
			return line_edit_delete_next_word(ll, l);
		case 'b':
			return line_edit_delete_prev_word(ll, l);
		case '0': /** @todo d0 **/
			break;
		case '$':
			buf[l->pos] = '\0';
			l->len = l->pos;
			return refresh_line(ll, l);
		case 'c':
		case 'd':
			buf[0] = '\0';
			l->pos = l->len = 0;
			return refresh_line(ll, l);
		default:
			ll->vi_escape = 1;
			return line_beep(ll);
		}
	}
	break;
	default:
		return line_beep(ll);
	}
	return 0;
}

/**@brief Remove a character from the current line
 * @param       l       linenoise state
 * @return      int     negative on error **/
static int line_edit_delete_char(libline_t *ll, struct line_state *l) {
	assert(ll);
	assert(l);
	if (l->len > 0) {
		line_edit_delete(ll, l);
		return 0;
	}
	if (ll->history_len > 0) {
		ll->history_len--;
		le_free(ll, ll->history[ll->history_len]);
		ll->history[ll->history_len] = NULL;
	}
	return -1;
}
/**@brief The core line editing function, most of the work is done here.
 *
 * This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
static int line_edit(libline_t *ll, char *buf, size_t buflen, const char *prompt) {
	assert(ll);
	assert(buf);
	assert(prompt);
	struct line_state l = { 0 };
	if (buflen < 80)
		return -1;

	/* Populate the linenoise state that we pass to functions implementing
	 * specific editing functionalities. */
	l.buf           = buf;
	l.buflen        = buflen;
	l.prompt        = prompt;
	l.plen          = strlen(prompt);
	l.oldpos        = 0;
	l.pos           = 0;
	l.len           = 0;
	l.cols          = get_columns(ll);
	l.maxrows       = 0;
	l.history_index = 0;

	/* Buffer starts empty. */
	l.buf[0] = '\0';
	l.buflen--; /* Make sure there is always space for the NUL terminator */

	/* The latest history entry is always our current buffer, that
	 * initially is just an empty string. */
	if (line_history_add(ll, "") < 0)
		return -1;
	if (le_write(ll, prompt, l.plen) == -1)
		return -1;
	while (1) {
		char c = 0;
		const int nread = le_read(ll, &c, 1);
		if (nread <= 0)
			return l.len;

		/* Only autocomplete when the callback is set. It returns < 0 when
		 * there was an error reading from fd. Otherwise it will return the
		 * character that should be handled next. */
		if (c == '\t' && ll->completion_callback != NULL) {
			c = complete_line(ll, &l);
			if (c < 0)
				return l.len;
			if (c == 0) /* Read next character when 0 */
				continue;
		}

		switch (c) {
		case ENTER:
			if (ll->history_len > 0) {
				ll->history_len--;
				le_free(ll, ll->history[ll->history_len]);
				ll->history[ll->history_len] = NULL;
			}
			return l.len;
		case CTRL_C:  
			/* errno = EAGAIN; */
			return -1;
		case BACKSPACE: /*fall through*/    
		case CTRL_H:
			line_edit_backspace(ll, &l);
			break;
		case CTRL_D: /* remove char at right of cursor, or of the
				line is empty, act as end-of-file. */
			if (line_edit_delete_char(ll, &l) < 0)
				return -1;
			break;

		case CTRL_T:  /*  swaps current character with previous. */
			if (l.pos > 0 && l.pos < l.len) {
				int aux = buf[l.pos - 1];
				buf[l.pos - 1] = buf[l.pos];
				buf[l.pos] = aux;
				if (l.pos != l.len - 1)
					l.pos++;
				if (refresh_line(ll, &l) < 0)
					return -1;
			}
			break;
		case CTRL_B:
			if (line_edit_move_left(ll, &l) < 0)
				return -1;
			break;
		case CTRL_F: 
			if (line_edit_move_right(ll, &l) < 0)
				return -1;
			break;
		case CTRL_P:
			if (line_edit_history_next(ll, &l, LINENOISE_HISTORY_PREV) < 0)
				return -1;
			break;
		case CTRL_N:
			if (line_edit_history_next(ll, &l, LINENOISE_HISTORY_NEXT) < 0)
				return -1;
			break;
		case CTRL_U:   /* delete the whole line. */
			buf[0] = '\0';
			l.pos = l.len = 0;
			if (refresh_line(ll, &l) < 0)
				return -1;
			break;
		case CTRL_K:   /* delete from current to end of line. */
			buf[l.pos] = '\0';
			l.len = l.pos;
			if (refresh_line(ll, &l) < 0)
				return -1;
			break;
		case CTRL_A:   /* go to the start of the line */
			if (line_edit_move_home(ll, &l) < 0)
				return -1;
			break;
		case CTRL_E:   /* go to the end of the line */
			if (line_edit_move_end(ll, &l) < 0)
				return -1;
			break;
		case CTRL_L:   /* clear screen */
			if (line_clearscreen(ll) < 0)
				return -1;
			if (refresh_line(ll, &l) < 0)
				return -1;
			break;
		case CTRL_W:   /* delete previous word */
			line_edit_delete_prev_word(ll, &l);
			break;
		case ESC: { /* begin escape sequence */

			/* Read the next two bytes representing the escape sequence.
			 * Use two calls to handle slow terminals returning the two
			 * chars at different times. */
			 
			char seq[3] = { 0 };
			if (le_read(ll, seq, 1) == -1)
				break;

			if ((0 == ll->vi_mode) || ('[' == seq[0]) || ('O' == seq[0])) {
				if (le_read(ll, seq + 1, 1) == -1)
					break;
			} else {
				ll->vi_escape = 1;
				if (line_edit_process_vi(ll, &l, seq[0], buf) < 0)
					return -1;
			}

			/* ESC [ sequences. */
			if (seq[0] == '[') {
				if (seq[1] >= '0' && seq[1] <= '9') {
					/* Extended escape, read additional byte. */
					if (le_read(ll, seq + 2, 1) == -1)
						break;
					if (seq[2] == '~') {
						switch (seq[1]) {
						case '3':      /* Delete key. */
							line_edit_delete(ll, &l);
							break;
						}
					}
				} else {
					switch (seq[1]) {
					case 'A':      /* Up */
						line_edit_history_next(ll, &l, LINENOISE_HISTORY_PREV);
						break;
					case 'B':      /* Down */
						line_edit_history_next(ll, &l, LINENOISE_HISTORY_NEXT);
						break;
					case 'C':      /* Right */
						line_edit_move_right(ll, &l);
						break;
					case 'D':      /* Left */
						line_edit_move_left(ll, &l);
						break;
					case 'H':      /* Home */
						line_edit_move_home(ll, &l);
						break;
					case 'F':      /* End */
						line_edit_move_end(ll, &l);
						break;
					}
				}
			} else if (seq[0] == 'O') { /* ESC O sequences. */
				switch (seq[1]) {
				case 'H':      /* Home */
					line_edit_move_home(ll, &l);
					break;
				case 'F':      /* End */
					line_edit_move_end(ll, &l);
					break;
				}
			} else if (0 != ll->vi_mode) {
				ll->vi_escape = 1;
				if (line_edit_process_vi(ll, &l, seq[0], buf) < 0) {
					return -1;
				}
			}
			break;
		}
		default:
			if ((0 == ll->vi_mode) || (0 == ll->vi_escape)) {
				if (line_edit_insert(ll, &l, c))
					return -1;
			} else { /*in vi command mode*/
				if (line_edit_process_vi(ll, &l, c, buf) < 0) {
					return -1;
				}
			}
			break;
		}
	}
	return l.len;
}

/** @brief This function calls the line editing function line_edit() using
 the STDIN file descriptor set in raw mode.  **/
static int line_raw(libline_t *ll, char *buf, size_t buflen, const char *prompt) {
	assert(buf);
	assert(prompt);
	if (buflen == 0) {
		/* errno = EINVAL; */
		return -1;
	}

	int count = 0;
	if (!le_isatty(ll, ll->in)) {
		if (fgets(buf, (int)buflen, stdin) == NULL) // !!
			return -1;
		count = strlen(buf);
		if (count && buf[count - 1] == '\n') {
			count--;
			buf[count] = '\0';
		}
	} else {
		/* Interactive editing. */
		count = line_edit(ll, buf, buflen, prompt);
		if (le_write(ll, "\n", 1) < 0)
			return -1;
	}
	return count;
}

/** @brief The main high level function of the linenoise library.
 *
 * The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses a dummy fgets() so that you will be able to type
 * something even in the most terminally desperate of conditions. **/
char *line_editor(libline_t *ll, const char *prompt) {
	assert(ll);
	assert(prompt);
	char buf[LINENOISE_MAX_LINE] = { 0 };
	if (le_is_unsupported_term()) {
		le_write(ll, prompt, strlen(prompt));
		if (!fgets(buf, LINENOISE_MAX_LINE, stdin)) // !!
			return NULL;
		size_t len = strlen(buf);
		while (len && ((buf[len - 1] == '\n') || (buf[len - 1] == '\r'))) {
			len--;
			buf[len] = '\0';
		}
		return le_strdup(ll, buf);
	} 
	const int count = line_raw(ll, buf, LINENOISE_MAX_LINE, prompt);
	if (count == -1)
		return NULL;
	return le_strdup(ll, buf);
}

/********************************* History ***********************************/

static void free_history(libline_t *ll) {
	assert(ll);
	if (ll->history) {
		for (int j = 0; j < ll->history_len; j++) {
			le_free(ll, ll->history[j]);
			ll->history[j] = NULL;
		}
		le_free(ll, ll->history);
		ll->history = NULL;
	}
	ll->history_len = 0;
}

/**@brief Add a line to the history.
 *
 * This is the API call to add a new entry in the linenoise history.
 * It uses a fixed array of char pointers that are shifted (memmoved)
 * when the history max length is reached in order to remove the older
 * entry and make room for the new one, so it is not exactly suitable for huge
 * histories, but will work well for a few hundred entries.
 *
 * Using a circular buffer is smarter, but a bit more complex to handle. **/
int line_history_add(libline_t *ll, const char *line) {
	assert(line);
	if (!(ll->history_on))
		return 0;
	if (ll->history_max_len == 0)
		return 0;

	/* Initialization on first call. */
	if (!ll->history) {
		ll->history = le_malloc(ll, sizeof(char *) * ll->history_max_len);
		if (!(ll->history))
			return -1;
		memset(ll->history, 0, (sizeof(char *) * ll->history_max_len));
	}

	/* Don't add duplicated lines. */
	if (ll->history_len && !strcmp(ll->history[ll->history_len - 1], line))
		return 0;

	/* Add an heap allocated copy of the line in the history.
	 * If we reached the max length, remove the older line. */
	char *linecopy = le_strdup(ll, line);
	if (!linecopy)
		return -1;
	if (ll->history_len == ll->history_max_len) {
		le_free(ll, ll->history[0]);
		memmove(ll->history, ll->history + 1, sizeof(char *) * (ll->history_max_len - 1));
		ll->history_len--;
	}
	ll->history[ll->history_len] = linecopy;
	ll->history_len++;
	return 1;
}

/** @brief Set the maximum history length, reducing it when necessary.
 *
 * Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. **/
int line_history_set_maxlen(libline_t *ll, int len) {
	assert(ll);
	if (++len < 1)
		return 0;
	if (ll->history) {
		char **new = le_malloc(ll, sizeof(char *) * len);
		if (!new)
			return 0;

		int tocopy = ll->history_len;
		/* If we can't copy everything, free the elements we do not use. */
		if (len < tocopy) {
			int j;
			for (j = 0; j < tocopy - len; j++)
				le_free(ll, ll->history[j]);
			tocopy = len;
		}
		memset(new, 0, sizeof(char *) * len);
		memcpy(new, ll->history + (ll->history_len - tocopy), sizeof(char *) * tocopy);
		le_free(ll, ll->history);
		ll->history = new;
	}
	ll->history_max_len = len;
	if (ll->history_len > ll->history_max_len)
		ll->history_len = ll->history_max_len;
	return 1;
}

/** @brief Save the history in the specified file. On success 0 is returned
 *	 otherwise -1 is returned. **/
int line_history_save(libline_t *ll, const char *filename) {
	assert(ll);
	assert(filename);
	FILE *fp = fopen(filename, "w");
	if (!fp)
		return -1;
	for (int j = 0; j < ll->history_len; j++) {
		fputs(ll->history[j], fp); 
		fputc('\n', fp);
	}
	fclose(fp);
	return 0;
}

/**@brief Load history from a file if the file exists
 *
 * Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int line_history_load(libline_t *ll, const char *filename) {
	assert(ll);
	assert(filename);
	int r = 0;
	char buf[LINENOISE_MAX_LINE] = { 0 };
	FILE *fp = fopen(filename, "r");
	if (!fp)
		return -1;
	while (fgets(buf, LINENOISE_MAX_LINE, fp) != NULL) {
		char *p = strchr(buf, '\r');
		if (!p)
			p = strchr(buf, '\n');
		if (p)
			*p = '\0';
		if (line_history_add(ll, buf) < 0) {
			r = -1;
			break;
		}
	}
	fclose(fp);
	return r;
}

void line_set_vi_mode(libline_t *ll, int on) {
	assert(ll);
	ll->vi_mode = on;
}

int line_get_vi_mode(libline_t *ll) {
	assert(ll);
	return ll->vi_mode;
}

int line_initialize(libline_t *ll) {
	assert(ll);
	memset(ll, 0, sizeof *ll);
	ll->history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
	ll->in              = STDIN_FILENO;
	ll->out             = STDOUT_FILENO;
	ll->history_on      = 1;
	return 0;
}

int line_cleanup(libline_t *ll) {
	assert(ll);
	free_history(ll);
	return 0;
}

