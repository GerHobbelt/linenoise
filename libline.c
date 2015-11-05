/** 
 * @file        libline.c 
 * @warning     This is a fork of the original! Do not contact the
 *              authors about bugs in *this* version!
 * @brief       A guerrilla line editing library against the idea that a
 *              line editing lib needs to be 20,000 lines of C code.
 * @author      Salvatore Sanfilippo
 * @author      Pieter Noordhuis
 * @author      Richard Howe
 * @license     BSD (included as comment)
 *
 * @todo There are a few vi commands that should be added (rR)
 * @todo The vi section and the rest should be separated.
 * @todo It would probably be best to split up this file into separate
 *       smaller ones.
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
 * <https://msdn.microsoft.com/en-us/library/windows/desktop/ms683231%28v=vs.85%29.aspx>
 *

 */

#include "libline.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifdef __unix__
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#elif _WIN32
#include <windows.h>
#include <io.h>
#include <conio.h>
#else
#error "Unsupported system"
#endif

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN (100)
#define LINENOISE_MAX_LINE                (4096)
#define LINENOISE_HISTORY_NEXT            (0)
#define LINENOISE_HISTORY_PREV            (1)
#define SEQ_BUF_LEN                       (64)

static char *unsupported_term[] = { "dumb", "cons25", "emacs", NULL };

static line_completion_callback *completion_callback = NULL;

#ifdef __unix__
static struct termios orig_termios;     /* In order to restore at exit. */
static int rawmode = 0;         /* For atexit() function to check if restore is needed */
#endif
static int atexit_registered = 0;       /* Register atexit just 1 time. */
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static int vi_mode = 0;         /* is vi mode on? */
static int vi_escape = 0;       /* are we in command or insert mode?*/
static char **history = NULL;

struct line_completions {
        size_t len;
        char **cvec;
};

/* The line_state structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct line_state {
        int ifd;                /* Terminal stdin file descriptor. */
        int ofd;                /* Terminal stdout file descriptor. */
        char *buf;              /* Edited line buffer. */
        size_t buflen;          /* Edited line buffer size. */
        const char *prompt;     /* Prompt to display. */
        size_t plen;            /* Prompt length. */
        size_t pos;             /* Current cursor position. */
        size_t oldpos;          /* Previous refresh cursor position. */
        size_t len;             /* Current edited line length. */
        size_t cols;            /* Number of columns in terminal. */
        size_t maxrows;         /* Maximum num of rows used so far (multiline mode) */
        int history_index;      /* The history index we are currently editing. */
};

enum KEY_ACTION {
        KEY_NULL = 0,           /* NULL */
        CTRL_A = 1,             /* Ctrl+a */
        CTRL_B = 2,             /* Ctrl-b */
        CTRL_C = 3,             /* Ctrl-c */
        CTRL_D = 4,             /* Ctrl-d */
        CTRL_E = 5,             /* Ctrl-e */
        CTRL_F = 6,             /* Ctrl-f */
        CTRL_H = 8,             /* Ctrl-h */
        TAB = 9,                /* Tab */
        CTRL_K = 11,            /* Ctrl+k */
        CTRL_L = 12,            /* Ctrl+l */
        ENTER = 13,             /* Enter */
        CTRL_N = 14,            /* Ctrl-n */
        CTRL_P = 16,            /* Ctrl-p */
        CTRL_T = 20,            /* Ctrl-t */
        CTRL_U = 21,            /* Ctrl+u */
        CTRL_W = 23,            /* Ctrl+w */
        ESC = 27,               /* Escape */
        BACKSPACE = 127         /* Backspace */
};

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf {
        size_t len;
        char *b;
};

static int complete_line(struct line_state *ls);
static int enable_raw_mode(int fd);
static int get_columns(int ifd, int ofd);
static int get_cursor_position(int ifd, int ofd);
static int is_unsupported_term(void);
static int line_edit(int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt);
static int line_edit_delete_char(struct line_state *l);
static int line_edit_process_vi(struct line_state *l, char c, char *buf);
static int line_raw(char *buf, size_t buflen, const char *prompt);

static void ab_append(struct abuf *ab, const char *s, size_t len);
static void ab_free(struct abuf *ab);
static void ab_init(struct abuf *ab);
static void disable_raw_mode(int fd);
static void free_completions(line_completions * lc);
static void free_history(void);
static void line_at_exit(void);
static void line_beep(void);
static void line_edit_next_word(struct line_state *l);
static void line_edit_prev_word(struct line_state *l);
static void refresh_line(struct line_state *l);

static char *le_strdup(const char *s) { 
        char *str;
        assert(s);
        if(!(str = malloc(strlen(s) + 1))) {
                fputs("Out of memory", stderr);
                exit(1); /*not the best thing to do in a library*/
        }
        strcpy(str, s);
        return str;
}

/************************* Low level terminal handling ***********************/

/**
 * @brief Return true if the terminal name is in the list of terminals 
 *        we know are not able to understand basic escape sequences.
 **/
static int is_unsupported_term(void)
{
        char *term = getenv("TERM");
        int j;

        if (term == NULL)
                return 0;
        for (j = 0; unsupported_term[j]; j++)
                if (!strcasecmp(term, unsupported_term[j]))
                        return 1;
        return 0;
}

/**
 * @brief Enable raw mode, which is a little arcane.
 **/
static int enable_raw_mode(int fd)
{
#ifdef __unix__
        struct termios raw;

        if (!isatty(STDIN_FILENO))
                goto fatal;
        if (!atexit_registered) {
                atexit(line_at_exit);
                atexit_registered = 1;
        }
        if (tcgetattr(fd, &orig_termios) == -1)
                goto fatal;

        raw = orig_termios;     /* modify the original mode */
        /* input modes: no break, no CR to NL, no parity check, no strip char,
         * no start/stop output control. */
        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        /* output modes - disable post processing */
        raw.c_oflag &= ~(OPOST);
        /* control modes - set 8 bit chars */
        raw.c_cflag |= (CS8);
        /* local modes - echoing off, canonical off, no extended functions,
         * no signal chars (^Z,^C) */
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        /* control chars - set return condition: min number of bytes and timer.
         * We want read to return every single byte, without timeout. */
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;    /* 1 byte, no timer */

        /* put terminal in raw mode after flushing */
        if (tcsetattr(fd, TCSAFLUSH, &raw) < 0)
                goto fatal;
        rawmode = 1;
        return 0;

 fatal:
        errno = ENOTTY;
        return -1;
#elif _WIN32
	(void)fd; /* parameter not used */
 	SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), 0);
        if (!atexit_registered) {
                atexit(line_at_exit);
                atexit_registered = 1;
        }
	return 0;
#endif
}

/** 
 * @brief like enable_raw_mode but the exact opposite 
 **/
static void disable_raw_mode(int fd)
{
#ifdef __unix__
        /* Don't even check the return value as it's too late. */
        if (rawmode && tcsetattr(fd, TCSAFLUSH, &orig_termios) != -1)
                rawmode = 0;
#elif _WIN32
	(void)fd;
#endif
}

/**
 * @brief Use the ESC [6n escape sequence to query the horizontal 
 *        cursor position and return it. On error -1 is returned, 
 *        on success the position of the cursor. 
 **/
static int get_cursor_position(int ifd, int ofd)
{
        char buf[32];
        int cols, rows;
        size_t i = 0;

        /* Report cursor location */
        if (write(ofd, "\x1b[6n", 4) != 4)
                return -1;

        /* Read the response: ESC [ rows ; cols R */
        while (i < sizeof(buf) - 1) {
                if (read(ifd, buf + i, 1) != 1)
                        break;
                if (buf[i] == 'R')
                        break;
                i++;
        }
        buf[i] = '\0';

        /* Parse it. */
        if (buf[0] != ESC || buf[1] != '[')
                return -1;
        if (sscanf(buf + 2, "%d;%d", &rows, &cols) != 2)
                return -1;
        return cols;
}

/** 
 * @brief Try to get the number of columns in the current terminal, or 
 *        assume 80 if it fails. 
 **/
static int get_columns(int ifd, int ofd)
{
#ifdef __unix__
        struct winsize ws;

        if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
                /* ioctl() failed. Try to query the terminal itself. */
                int start, cols;

                /* Get the initial position so we can restore it later. */
                start = get_cursor_position(ifd, ofd);
                if (start == -1)
                        goto failed;

                /* Go to right margin and get position. */
                if (write(ofd, "\x1b[999C", 6) != 6)
                        goto failed;
                cols = get_cursor_position(ifd, ofd);
                if (cols == -1)
                        goto failed;

                /* Restore position. */
                if (cols > start) {
                        char seq[32];
                        snprintf(seq, 32, "\x1b[%dD", cols - start);
                        if (write(ofd, seq, strlen(seq)) == -1) {
                                /* Can't recover... */
                        }
                }
                return cols;
        } else {
                return (int)ws.ws_col;
        }

 failed:
        return 80;
#elif _WIN32
	(void)ifd; 
	(void)ofd;
	return 80;
#endif
}

/** 
 * @brief Clear the screen. Used to handle ctrl+l 
 **/
void line_clearscreen(void)
{
#ifdef __unix__
        if (write(STDOUT_FILENO, "\x1b[H\x1b[2J", 7) <= 0) {
                /* nothing to do, just to avoid warning. */
        }
#elif _WIN32
	/*clrscr();*/
#endif
}

/** 
 * @brief Beep, used for completion when there is nothing to complete 
 * or when all the choices were already shown. 
 **/
static void line_beep(void)
{
#ifdef __unix__
        fputs("\x7",stderr);
        fflush(stderr);
#elif _WIN32
        /*beep();*/
#endif
}

/******************************** Completion *********************************/

/** 
 * @brief Free a list of completion options populated by 
 *        line_add_completion(). 
 **/
static void free_completions(line_completions * lc)
{
        size_t i;
        for (i = 0; i < lc->len; i++)
                free(lc->cvec[i]);
        if (lc->cvec != NULL)
                free(lc->cvec);
}

/** 
 * @brief This is an helper function for line_edit() and is called when the
 *        user types the <tab> key in order to complete the string currently 
 *        in the input. The state of the editing is encapsulated into the pointed 
 *        line_state structure as described in the structure definition. 
 **/
static int complete_line(struct line_state *ls)
{
        line_completions lc = { 0, NULL };
        ssize_t nread;
        int nwritten;
        char c = 0;

        completion_callback(ls->buf, ls->pos, &lc);
        if (lc.len == 0) {
                line_beep();
        } else {
                size_t stop = 0, i = 0;

                while (!stop) {
                        /* Show completion or original buffer */
                        if (i < lc.len) {
                                struct line_state saved = *ls;

                                ls->len = ls->pos = strlen(lc.cvec[i]);
                                ls->buf = lc.cvec[i];
                                refresh_line(ls);
                                ls->len = saved.len;
                                ls->pos = saved.pos;
                                ls->buf = saved.buf;
                        } else {
                                refresh_line(ls);
                        }

                        nread = read(ls->ifd, &c, 1);
                        if (nread <= 0) {
                                free_completions(&lc);
                                return -1;
                        }

                        switch (c) {
                        case TAB:        /* tab */
                                i = (i + 1) % (lc.len + 1);
                                if (i == lc.len)
                                        line_beep();
                                break;
                        case ESC:       /* escape */
                                /* Re-show original buffer */
                                if (i < lc.len)
                                        refresh_line(ls);
                                stop = 1;
                                break;
                        default:
                                /* Update buffer and return */
                                if (i < lc.len) {
                                        nwritten = snprintf(ls->buf, ls->buflen, "%s", lc.cvec[i]);
                                        ls->len = ls->pos = (size_t)nwritten;
                                }
                                stop = 1;
                                break;
                        }
                }
        }

        free_completions(&lc);
        return c; /* Return last read character */
}

/** 
 * @brief Register a callback function to be called for tab-completion. 
 **/
void line_set_completion_callback(line_completion_callback * fn)
{
        completion_callback = fn;
}

/**
 * @brief This function is used by the callback function registered by the user
 *        in order to add completion options given the input string when the
 *        user typed <tab>. See the example.c source code for a very easy to
 *        understand example.
 **/
void line_add_completion(line_completions * lc, const char *str)
{
        size_t len = strlen(str);
        char *copy, **cvec;

        copy = malloc(len + 1);
        if (copy == NULL)
                return;
        memcpy(copy, str, len + 1);
        cvec = realloc(lc->cvec, sizeof(char *) * (lc->len + 1));
        if (cvec == NULL) {
                free(copy);
                return;
        }
        lc->cvec = cvec;
        lc->cvec[lc->len++] = copy;
}

/***************************** Line editing **********************************/

/**
 * @brief Initialize a abuf structure to zero
 **/
static void ab_init(struct abuf *ab)
{
        ab->b = NULL;
        ab->len = 0;
}

/**
 * @brief Append a string to an abuf struct
 **/
static void ab_append(struct abuf *ab, const char *s, size_t len)
{
        char *new = realloc(ab->b, ab->len + len);

        if (new == NULL)
                return;
        memcpy(new + ab->len, s, len);
        ab->b = new;
        ab->len += len;
}

/**
 * @brief Handle abuf memory freeing
 **/
static void ab_free(struct abuf *ab)
{
        free(ab->b);
        ab->b = NULL;
}

/**
 * @brief Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. 
 **/
static void refresh_line(struct line_state *l)
{
        char seq[SEQ_BUF_LEN];
        size_t plen = strlen(l->prompt);
        int fd = l->ofd;
        char *buf = l->buf;
        size_t len = l->len;
        size_t pos = l->pos;
        struct abuf ab;

        if((plen + pos) >= l->cols){
                size_t take = (plen + pos) - l->cols;
                len -= take;
                pos -= take;
                buf += take;
        }

        if(plen + len > l->cols)
                len = l->cols - plen;

        ab_init(&ab);
        /* Cursor to left edge */
        snprintf(seq, SEQ_BUF_LEN, "\x1b[0G");
        ab_append(&ab, seq, strlen(seq));
        /* Write the prompt and the current buffer content */
        ab_append(&ab, l->prompt, strlen(l->prompt));
        ab_append(&ab, buf, len);
        /* Erase to right */
        snprintf(seq, SEQ_BUF_LEN, "\x1b[0K");
        ab_append(&ab, seq, strlen(seq));
        /* Move cursor to original position. */
        snprintf(seq, SEQ_BUF_LEN, "\x1b[0G\x1b[%dC", (int)(pos + plen));
        ab_append(&ab, seq, strlen(seq));
        if (write(fd, ab.b, ab.len) == -1) {
        }                       /* Can't recover from write error. */
        ab_free(&ab);
}

/**
 * @brief Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. 
 **/
int line_edit_insert(struct line_state *l, char c)
{
        if (l->len < l->buflen) {
                if (l->len == l->pos) {
                        l->buf[l->pos] = c;
                        l->pos++;
                        l->len++;
                        l->buf[l->len] = '\0';
                        if ((l->plen + l->len) < l->cols) {
                                /* Avoid a full update of the line in the
                                 * trivial case. */
                                if (write(l->ofd, &c, 1) == -1)
                                        return -1;
                        } else {
                                refresh_line(l);
                        }
                } else {
                        memmove(l->buf + l->pos + 1, l->buf + l->pos, l->len - l->pos);
                        l->buf[l->pos] = c;
                        l->len++;
                        l->pos++;
                        l->buf[l->len] = '\0';
                        refresh_line(l);
                }
        }
        return 0;
}

/** 
 * @brief Move cursor on the left.
 **/
void line_edit_move_left(struct line_state *l)
{
        if (l->pos > 0) {
                l->pos--;
                refresh_line(l);
        }
}

/** 
 * @brief Move cursor on the right. 
 **/
void line_edit_move_right(struct line_state *l)
{
        if (l->pos != l->len) {
                l->pos++;
                refresh_line(l);
        }
}

/**
 * @brief Move cursor to the start of the line. 
 **/
void line_edit_move_home(struct line_state *l)
{
        if (l->pos != 0) {
                l->pos = 0;
                refresh_line(l);
        }
}

/** 
 * @brief Move cursor to the end of the line. 
 **/
void line_edit_move_end(struct line_state *l)
{
        if (l->pos != l->len) {
                l->pos = l->len;
                refresh_line(l);
        }
}

/**
 * @brief Substitute the currently edited line with the next or 
 *        previous history entry as specified by 'dir'. 
 **/
void line_edit_history_next(struct line_state *l, int dir)
{
        if (history_len > 1) {
                /* Update the current history entry before to
                 * overwrite it with the next one. */
                free(history[history_len - 1 - l->history_index]);
                history[history_len - 1 - l->history_index] = le_strdup(l->buf);
                /* Show the new entry */
                l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
                if (l->history_index < 0) {
                        l->history_index = 0;
                        return;
                } else if (l->history_index >= history_len) {
                        l->history_index = history_len - 1;
                        return;
                }
                strncpy(l->buf, history[history_len - 1 - l->history_index], l->buflen);
                l->buf[l->buflen - 1] = '\0';
                l->len = l->pos = strlen(l->buf);
                refresh_line(l);
        }
}

/** 
 * @brief Delete the character at the right of the cursor without altering the 
 *        cursor position. Basically this is what happens with the "Delete" 
 *        keyboard key. 
 **/
void line_edit_delete(struct line_state *l)
{
        if (l->len > 0 && l->pos < l->len) {
                memmove(l->buf + l->pos, l->buf + l->pos + 1, l->len - l->pos - 1);
                l->len--;
                l->buf[l->len] = '\0';
                refresh_line(l);
        }
}

/** 
 * @brief Backspace implementation. 
 **/
void line_edit_backspace(struct line_state *l)
{
        if (l->pos > 0 && l->len > 0) {
                memmove(l->buf + l->pos - 1, l->buf + l->pos, l->len - l->pos);
                l->pos--;
                l->len--;
                l->buf[l->len] = '\0';
                refresh_line(l);
        }
}

/**
 * @brief Move the cursor to the next word
 **/
static void line_edit_next_word(struct line_state *l){
        while ((l->pos < l->len) && (' ' == l->buf[l->pos + 1]))
                l->pos++;
        while ((l->pos < l->len) && (' ' != l->buf[l->pos + 1]))
                l->pos++;
        if(l->pos < l->len)
                l->pos++;
}

/**
 * @brief Delete the next word, maintaining the cursor at the start 
 *        of the current word. 
 **/
void line_edit_delete_next_word(struct line_state *l)
{
        size_t old_pos = l->pos;
        size_t diff;
        
        line_edit_next_word(l);

        diff = l->pos - old_pos;
        memmove(l->buf + old_pos, l->buf + l->pos, l->len - old_pos + 1);
        l->len -= diff;
        l->pos = old_pos;
        refresh_line(l);
}

/**
 * @brief Move the cursor to the previous word.
 **/
static void line_edit_prev_word(struct line_state *l){
        while ((l->pos > 0) && (l->buf[l->pos - 1] == ' '))
                l->pos--;
        while ((l->pos > 0) && (l->buf[l->pos - 1] != ' '))
                l->pos--;
}

/**
 * @brief Delete the previous word, maintaining the cursor at the start 
 *        of the current word. 
 **/
void line_edit_delete_prev_word(struct line_state *l)
{
        size_t old_pos = l->pos;
        size_t diff;

        line_edit_prev_word(l);

        diff = old_pos - l->pos;
        memmove(l->buf + l->pos, l->buf + old_pos, l->len - old_pos + 1);
        l->len -= diff;
        refresh_line(l);
}

/**
 * @brief This function processes vi-key commands. 
 * @param  l    The state of the line editor
 * @param  c    Current keyboard character we are processing
 * @param  buf  Input buffer
 * @return int  -1 on error
 **/
static int line_edit_process_vi(struct line_state *l, char c, char *buf){
        switch(c){
        case 'x': /*delete char*/
                if(l->pos && (l->pos == l->len))
                        l->pos--;
                (void)line_edit_delete_char(l);
                break;
        case 'w': /* move forward a word */
                line_edit_next_word(l);
                refresh_line(l);
                break;
        case 'b': /* move back a word */
                line_edit_prev_word(l);
                refresh_line(l);
                break;
        case 'C': /*Change*/
                vi_escape = 0;
                /*fall through*/
        case 'D': /*Delete from cursor to the end of the line*/
                buf[l->pos] = '\0';
                l->len = l->pos;
                refresh_line(l);
                break;
        case '0': /*Go to the beginning of the line*/
                line_edit_move_home(l);
                break;
        case '$': /*move to the end of the line*/
                line_edit_move_end(l);
                break;
        case 'l': /*move right*/
                line_edit_move_right(l);
                break;
        case 'h': /*move left*/
                line_edit_move_left(l);
                break;
        case 'A':/*append at end of line*/
                l->pos = l->len;
                refresh_line(l);
                /*fall through*/
        case 'a':/*append after the cursor*/
                if(l->pos != l->len){
                        l->pos++;
                        refresh_line(l);
                }
                /*fall through*/
        case 'i':/*insert text before the cursor*/
                vi_escape = 0;
                break;
        case 'I':/*Insert text before the first non-blank in the line*/
                vi_escape = 0;
                l->pos = 0;
                refresh_line(l);
                break;
        case 'k': /*move up*/
                line_edit_history_next(l, LINENOISE_HISTORY_PREV);
                break;
        case 'r':
        {       /*replace a character*/
                int replace = 0;
                if (read(l->ifd, &replace, 1) == -1)
                        break;
                buf[l->pos] = replace;
                refresh_line(l);
                break;
        }
        case 'j': /*move down*/
                line_edit_history_next(l, LINENOISE_HISTORY_NEXT);
                break;
        case 'f': /*fall through*/
        case 'F': /*fall through*/
        case 't': /*fall through*/
        case 'T': /*fall through*/
        {
                ssize_t dir, lim, cpos;
                int find = 0; 

                if (read(l->ifd,&find,1) == -1) 
                        break;

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
                        refresh_line(l);
                        break;
                    }
                }

                if (cpos == lim) 
                        line_beep();
        }
        break;
        case 'c':
                vi_escape = 0;
        case 'd': /*delete*/
        {
                char rc[1];
                if (read(l->ifd, rc, 1) == -1)
                        break;
                switch(rc[0]){
                case 'w': 
                        line_edit_delete_next_word(l);
                        break;
                case 'b':
                        line_edit_delete_prev_word(l);
                        break;
                case '0': /** @todo d0 **/
                        break;
                case '$':
                        buf[l->pos] = '\0';
                        l->len = l->pos;
                        refresh_line(l);
                        break;
                case 'c':
                case 'd':
                        buf[0] = '\0';
                        l->pos = l->len = 0;
                        refresh_line(l);
                        break;
                default:
                        line_beep();
                        vi_escape = 1;
                        break;
                }
        }
        break;
        default:
                line_beep();
                break;

        }
        return 0;
}

/**
 * @brief Remove a character from the current line
 * @param       l       linenoise state
 * @return      int     negative on error
 **/
static int line_edit_delete_char(struct line_state *l){
        if (l->len > 0) {
                line_edit_delete(l);
        } else {
                history_len--;
                free(history[history_len]);
                history[history_len] = NULL;
                return -1;
        }

        return 0;
}
/**
 * @brief The core line editing function, most of the work is done here.
 *
 * This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
static int line_edit(int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt)
{
        struct line_state l;

        /* Populate the linenoise state that we pass to functions implementing
         * specific editing functionalities. */
        l.ifd = stdin_fd;
        l.ofd = stdout_fd;
        l.buf = buf;
        l.buflen = buflen;
        l.prompt = prompt;
        l.plen = strlen(prompt);
        l.oldpos = l.pos = 0;
        l.len = 0;
        l.cols = get_columns(stdin_fd, stdout_fd);
        l.maxrows = 0;
        l.history_index = 0;

        /* Buffer starts empty. */
        l.buf[0] = '\0';
        l.buflen--; /* Make sure there is always space for the nulterm */

        /* The latest history entry is always our current buffer, that
         * initially is just an empty string. */
        line_history_add("");

        if (write(l.ofd, prompt, l.plen) == -1)
                return -1;
        while (1) {
                char c;
                int nread;
                char seq[3];

                nread = read(l.ifd, &c, 1);
                if (nread <= 0)
                        return l.len;

                /* Only autocomplete when the callback is set. It returns < 0 when
                 * there was an error reading from fd. Otherwise it will return the
                 * character that should be handled next. */
                if (c == '\t' && completion_callback != NULL) {
                        c = complete_line(&l);
                        /* Return on errors */
                        if (c < 0)
                                return l.len;
                        /* Read next character when 0 */
                        if (c == 0)
                                continue;
                }

                switch (c) {
                case ENTER:  
                        history_len--;
                        free(history[history_len]);
                        history[history_len] = NULL;
                        return l.len;
                case CTRL_C:  
                        errno = EAGAIN;
                        return -1;
                case BACKSPACE: /*fall through*/    
                case CTRL_H:        
                        line_edit_backspace(&l);
                        break;
                case CTRL_D: /* remove char at right of cursor, or of the
                                line is empty, act as end-of-file. */
                        if(line_edit_delete_char(&l) < 0)
                                return -1;
                        break;

                case CTRL_T:  /*  swaps current character with previous. */
                        if (l.pos > 0 && l.pos < l.len) {
                                int aux = buf[l.pos - 1];
                                buf[l.pos - 1] = buf[l.pos];
                                buf[l.pos] = aux;
                                if (l.pos != l.len - 1)
                                        l.pos++;
                                refresh_line(&l);
                        }
                        break;
                case CTRL_B:
                        line_edit_move_left(&l);
                        break;
                case CTRL_F: 
                        line_edit_move_right(&l);
                        break;
                case CTRL_P:
                        line_edit_history_next(&l, LINENOISE_HISTORY_PREV);
                        break;
                case CTRL_N:
                        line_edit_history_next(&l, LINENOISE_HISTORY_NEXT);
                        break;
                case CTRL_U:   /* delete the whole line. */
                        buf[0] = '\0';
                        l.pos = l.len = 0;
                        refresh_line(&l);
                        break;
                case CTRL_K:   /* delete from current to end of line. */
                        buf[l.pos] = '\0';
                        l.len = l.pos;
                        refresh_line(&l);
                        break;
                case CTRL_A:   /* go to the start of the line */
                        line_edit_move_home(&l);
                        break;
                case CTRL_E:   /* go to the end of the line */
                        line_edit_move_end(&l);
                        break;
                case CTRL_L:   /* clear screen */
                        line_clearscreen();
                        refresh_line(&l);
                        break;
                case CTRL_W:   /* delete previous word */
                        line_edit_delete_prev_word(&l);
                        break;
                case ESC: /* begin escape sequence */

                        /* Read the next two bytes representing the escape sequence.
                         * Use two calls to handle slow terminals returning the two
                         * chars at different times. */
                         
                        if (read(l.ifd, seq, 1) == -1)
                                break;

                        if((0 == vi_mode) || ('[' == seq[0]) || ('O' == seq[0])){
                                if (read(l.ifd, seq + 1, 1) == -1)
                                        break;
                        } else {
                                vi_escape = 1;
                                if(line_edit_process_vi(&l, seq[0], buf) < 0){
                                        return -1;
                                }
                        }

                        /* ESC [ sequences. */
                        if (seq[0] == '[') {
                                if (seq[1] >= '0' && seq[1] <= '9') {
                                        /* Extended escape, read additional byte. */
                                        if (read(l.ifd, seq + 2, 1) == -1)
                                                break;
                                        if (seq[2] == '~') {
                                                switch (seq[1]) {
                                                case '3':      /* Delete key. */
                                                        line_edit_delete(&l);
                                                        break;
                                                }
                                        }
                                } else {
                                        switch (seq[1]) {
                                        case 'A':      /* Up */
                                                line_edit_history_next(&l, LINENOISE_HISTORY_PREV);
                                                break;
                                        case 'B':      /* Down */
                                                line_edit_history_next(&l, LINENOISE_HISTORY_NEXT);
                                                break;
                                        case 'C':      /* Right */
                                                line_edit_move_right(&l);
                                                break;
                                        case 'D':      /* Left */
                                                line_edit_move_left(&l);
                                                break;
                                        case 'H':      /* Home */
                                                line_edit_move_home(&l);
                                                break;
                                        case 'F':      /* End */
                                                line_edit_move_end(&l);
                                                break;
                                        }
                                }
                        } else if (seq[0] == 'O') { /* ESC O sequences. */
                                switch (seq[1]) {
                                case 'H':      /* Home */
                                        line_edit_move_home(&l);
                                        break;
                                case 'F':      /* End */
                                        line_edit_move_end(&l);
                                        break;
                                }
                        } else if(0 != vi_mode){
                                vi_escape = 1;
                                if(line_edit_process_vi(&l, seq[0], buf) < 0){
                                        return -1;
                                }
                        }
                        break;
                default:
                        if((0 == vi_mode) || (0 == vi_escape)){
                                if (line_edit_insert(&l, c))
                                        return -1;
                        } else { /*in vi command mode*/
                                if(line_edit_process_vi(&l, c, buf) < 0){
                                        return -1;
                                }
                        }
                        break;
                }
        }
        return l.len;
}

/**
 * @brief This function calls the line editing function line_edit() using
 *        the STDIN file descriptor set in raw mode. 
 **/
static int line_raw(char *buf, size_t buflen, const char *prompt)
{
        int count;

        if (buflen == 0) {
                errno = EINVAL;
                return -1;
        }
        if (!isatty(STDIN_FILENO)) {
                /* Not a tty: read from file / pipe. */
                if (fgets(buf, (int)buflen, stdin) == NULL)
                        return -1;
                count = strlen(buf);
                if (count && buf[count - 1] == '\n') {
                        count--;
                        buf[count] = '\0';
                }
        } else {
                /* Interactive editing. */
                if (enable_raw_mode(STDIN_FILENO) == -1)
                        return -1;
                count = line_edit(STDIN_FILENO, STDOUT_FILENO, buf, buflen, prompt);
                disable_raw_mode(STDIN_FILENO);
                putchar('\n');
        }
        return count;
}

/**
 * @brief The main high level function of the linenoise library.
 *
 * The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses a dummy fgets() so that you will be able to type
 * something even in the most terminally desperate of conditions. 
 **/
char *line_editor(const char *prompt)
{
        char buf[LINENOISE_MAX_LINE];
        int count;

        if (is_unsupported_term()) {
                size_t len;

                fputs(prompt,stdout);
                fflush(stdout);
                if (fgets(buf, LINENOISE_MAX_LINE, stdin) == NULL)
                        return NULL;
                len = strlen(buf);
                while (len && ((buf[len - 1] == '\n') || (buf[len - 1] == '\r'))) {
                        len--;
                        buf[len] = '\0';
                }
                return le_strdup(buf);
        } else {
                count = line_raw(buf, LINENOISE_MAX_LINE, prompt);
                if (count == -1)
                        return NULL;
                return le_strdup(buf);
        }
}

/********************************* History ***********************************/

/**
 * @brief Free the history, but does not reset it. Only used when we have to
 *        exit() to avoid memory leaks that are reported by valgrind & co. 
 **/
static void free_history(void)
{
        if (history) {
                int j;

                for (j = 0; j < history_len; j++) {
                        free(history[j]);
                        history[j] = NULL;
                }
                free(history);
                history = NULL;
        }
}

/**
 * @brief At exit we'll try to fix the terminal to the initial conditions. 
 **/
static void line_at_exit(void)
{
#ifdef __unix__
        if(rawmode)
                disable_raw_mode(STDIN_FILENO);
#endif
        free_history();
}

/** 
 * @brief Add a line to the history.
 *
 * This is the API call to add a new entry in the linenoise history.
 * It uses a fixed array of char pointers that are shifted (memmoved)
 * when the history max length is reached in order to remove the older
 * entry and make room for the new one, so it is not exactly suitable for huge
 * histories, but will work well for a few hundred entries.
 *
 * Using a circular buffer is smarter, but a bit more complex to handle. 
 **/
int line_history_add(const char *line)
{
        char *linecopy;

        if (history_max_len == 0)
                return 0;

        /* Initialization on first call. */
        if (history == NULL) {
                history = malloc(sizeof(char *) * history_max_len);
                if (history == NULL)
                        return 0;
                memset(history, 0, (sizeof(char *) * history_max_len));
        }

        /* Don't add duplicated lines. */
        if (history_len && !strcmp(history[history_len - 1], line))
                return 0;

        /* Add an heap allocated copy of the line in the history.
         * If we reached the max length, remove the older line. */
        linecopy = le_strdup(line);
        if (!linecopy)
                return 0;
        if (history_len == history_max_len) {
                free(history[0]);
                memmove(history, history + 1, sizeof(char *) * (history_max_len - 1));
                history_len--;
        }
        history[history_len] = linecopy;
        history_len++;
        return 1;
}

/**
 * @brief Set the maximum history length, reducing it when necessary.
 *
 * Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. 
 *
 **/
int line_history_set_maxlen(int len)
{
        char **new;

        if (++len < 1)
                return 0;
        if (history) {
                int tocopy = history_len;

                new = malloc(sizeof(char *) * len);
                if (new == NULL)
                        return 0;

                /* If we can't copy everything, free the elements we do not use. */
                if (len < tocopy) {
                        int j;
                        for (j = 0; j < tocopy - len; j++)
                                free(history[j]);
                        tocopy = len;
                }
                memset(new, 0, sizeof(char *) * len);
                memcpy(new, history + (history_len - tocopy), sizeof(char *) * tocopy);
                free(history);
                history = new;
        }
        history_max_len = len;
        if (history_len > history_max_len)
                history_len = history_max_len;
        return 1;
}

/**
 * @brief Save the history in the specified file. On success 0 is returned
 *         otherwise -1 is returned. 
 **/
int line_history_save(const char *filename)
{
        FILE *fp = fopen(filename, "w");
        int j;

        if (fp == NULL)
                return -1;
        for (j = 0; j < history_len; j++)
                fputs(history[j], fp), fputc('\n', fp);
        fclose(fp);
        return 0;
}

/**
 * @brief Load history from a file if the file exists
 *
 * Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int line_history_load(const char *filename)
{
        FILE *fp = fopen(filename, "r");
        char buf[LINENOISE_MAX_LINE];

        if (fp == NULL)
                return -1;

        while (fgets(buf, LINENOISE_MAX_LINE, fp) != NULL) {
                char *p;

                p = strchr(buf, '\r');
                if (!p)
                        p = strchr(buf, '\n');
                if (p)
                        *p = '\0';
                line_history_add(buf);
        }
        fclose(fp);

        if(!atexit_registered)
                atexit(line_at_exit);

        return 0;
}

/**
 * @brief Turn on 'vi' editing mode
 **/
void line_set_vi_mode(int on){
        vi_mode = on;
}

int line_get_vi_mode(void) {
        return vi_mode;
}

