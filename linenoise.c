/* linenoise.c -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * You can find the latest source code at:
 * 
 *   http://github.com/antirez/linenoise
 *
 * Does a number of crazy assumptions that happen to be true in 99.9999% of
 * the 2010 UNIX computers around.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2013, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2013, Oldrich Jedlicka <oldium dot pro at seznam dot cz>
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
 * Todo list:
 * - Filter bogus Ctrl+<char> combinations.
 * - Win32 support
 *
 * Bloat:
 * - History search like Ctrl+r in readline?
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
 */

#define _GNU_SOURCE

#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <locale.h>
#include "linenoise.h"

typedef struct linenoiseSingleCompletion {
    char *text;
    size_t pos;
} linenoiseSingleCompletion;

struct linenoiseCompletions {
  size_t len;
  size_t max_strlen;
  linenoiseSingleCompletion *cvec;
};

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_LINE_INIT_MAX_AND_GROW 4096
#define LINENOISE_COL_SPACING 2
#define MIN(a, b) ((a) < (b) ? (a) : (b))
static char *unsupported_term[] = {"dumb","cons25",NULL};
static linenoiseCompletionCallback *completionCallback = NULL;

static struct termios orig_termios; /* In order to restore at exit.*/
static int rawmode = 0; /* For atexit() function to check if restore is needed*/
static int mlmode = 0;  /* Multi line mode. Default is single line. */
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
char **history = NULL;

enum LinenoiseState {
    LS_NEW_LINE,
    LS_READ
};

/* The linenoiseState structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct linenoiseState {
    int fd;             /* Terminal file descriptor. */
    char *buf;          /* Edited line buffer. */
    size_t buflen;      /* Edited line buffer size. */
    char *prompt;       /* Prompt to display. */
    size_t plen;        /* Prompt length. */
    size_t pos;         /* Current cursor position. */
    size_t oldpos;      /* Previous refresh cursor position. */
    size_t len;         /* Current edited line length. */
    size_t cols;        /* Number of columns in terminal. */
    size_t maxrows;     /* Maximum num of rows used so far (multiline mode) */
    int history_index;  /* The history index we are currently editing. */
    bool needs_refresh; /* True when the lines need to be refreshed. */
    bool is_displayed;  /* True when the prompt has been displayed. */
    enum LinenoiseState state;    /* Internal state. */
};

static struct linenoiseState state;
static bool initialized = false;

static void linenoiseAtExit(void);
static int refreshLine(struct linenoiseState *l);
static int initialize(const char *prompt);
static int ensureBufLen(struct linenoiseState *l, size_t requestedStrLen);
static int prepareCustomOutput(struct linenoiseState *l);

enum SpecialCharacters
{
    SC_ERROR    = -1,
    SC_CLOSED    = -2,
};

/* ======================= Low level terminal handling ====================== */

/* Set if to use or not the multi line mode. */
void linenoiseSetMultiLine(int ml) {
    mlmode = ml;
}

/* Return true if the terminal is not a TTY or the name is in the list of
 * terminals we know are not able to understand basic escape sequences. */
static int isUnsupportedTerm(void) {
    if ( !isatty(STDIN_FILENO) )
        return 1;

    char *term = getenv("TERM");
    int j;

    if (term == NULL) return 0;
    for (j = 0; unsupported_term[j]; j++)
        if (!strcasecmp(term,unsupported_term[j])) return 1;
    return 0;
}

/* Raw mode: 1960 magic shit. */
static int enableRawMode(int fd) {
    struct termios raw;

    if (tcgetattr(fd,&orig_termios) == -1) goto fatal;

    raw = orig_termios;  /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    raw.c_oflag &= ~(OPOST);
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* control chars - set return condition: min number of bytes and timer.
     * We want read to return every single byte, without timeout. */
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

    /* put terminal in raw mode after flushing */
    if (tcsetattr(fd,TCSAFLUSH,&raw) < 0) goto fatal;
    rawmode = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

static void disableRawMode(int fd) {
    /* Don't even check the return value as it's too late. */
    if (rawmode && tcsetattr(fd,TCSAFLUSH,&orig_termios) != -1)
        rawmode = 0;
}

/* Try to get the number of columns in the current terminal, or assume 80
 * if it fails. */
static int getColumns(void) {
    struct winsize ws;

    if (TEMP_FAILURE_RETRY(ioctl(1, TIOCGWINSZ, &ws)) == -1 || ws.ws_col == 0) return 80;
    return ws.ws_col;
}

/* Clear the screen. Used to handle ctrl+l */
int linenoiseClearScreen(void) {
    if (TEMP_FAILURE_RETRY(write(STDIN_FILENO,"\x1b[H\x1b[2J",7)) <= 0) {
        /* nothing to do, just to avoid warning. */
    }
    return 0;
}

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
static int linenoiseBeep(void) {
    if (fprintf(stderr, "\x7") < 0) return -1;
    if (TEMP_FAILURE_RETRY(fflush(stderr)) == -1) return -1;
    return 0;
}

/* ============================== Completion ================================ */

/* Free a list of completion option populated by linenoiseAddCompletion(). */
static void freeCompletions(linenoiseCompletions *lc) {
    size_t i;
    for (i = 0; i < lc->len; i++)
        free(lc->cvec[i].text);
    free(lc->cvec);
}

static int completitionCompare(const void *first, const void *second)
{
    linenoiseSingleCompletion *firstcomp = (linenoiseSingleCompletion *) first;
    linenoiseSingleCompletion *secondcomp = (linenoiseSingleCompletion *) second;
    return (strcoll(firstcomp->text, secondcomp->text));
}

/* This is an helper function for linenoiseEdit() and is called when the
 * user types the <tab> key in order to complete the string currently in the
 * input.
 * 
 * The state of the editing is encapsulated into the pointed linenoiseState
 * structure as described in the structure definition. */
static int completeLine(struct linenoiseState *ls) {
    linenoiseCompletions lc = { 0, 0, NULL };
    char c = 0;

    completionCallback(ls->buf, ls->pos, &lc);

    if (lc.len > 0 && lc.cvec == NULL) {
        errno = ENOMEM;
        return -1;
    } else if (lc.len == 0) {
        if (linenoiseBeep() == -1) goto err_cleanup;
    } else if (lc.len == 1) {
        // Simple case
        size_t new_strlen = strlen(lc.cvec[0].text);
        if (ensureBufLen(ls, new_strlen) == -1) return -1;

        memcpy(ls->buf, lc.cvec[0].text, new_strlen+1);
        ls->pos = MIN(new_strlen, lc.cvec[0].pos);
        ls->len = new_strlen;
        if (refreshLine(ls) == -1) goto err_cleanup;
    } else {
        // Multiple choices - sort them and print
        if (prepareCustomOutput(ls) == -1) return -1;

        qsort(lc.cvec, lc.len, sizeof(linenoiseSingleCompletion), completitionCompare);
        size_t colSize = lc.max_strlen + LINENOISE_COL_SPACING;
        size_t cols = ls->cols / colSize;
        if (cols == 0)
            cols = 1;
        size_t rows = (lc.len + cols - 1) / cols;
        size_t i;

        for (i = 0; i < lc.len; i++)
        {
            size_t real_index = (i % cols) * rows + i / cols;
            if (real_index < lc.len)
                printf("%-*s", (int)colSize, lc.cvec[real_index].text);
            if ((i % cols) == (cols - 1))
                printf("\r\n");
        }
        if ((i % cols) != 0)
            printf("\r\n");
        if (refreshLine(ls) == -1) return -1;
    }

    freeCompletions(&lc);
    return c; /* Return last read character */

err_cleanup:
    freeCompletions(&lc);
    return -1;
}

/* Register a callback function to be called for tab-completion. */
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn) {
    completionCallback = fn;
}

/* This function is used by the callback function registered by the user
 * in order to add completion options given the input string when the
 * user typed <tab>. See the example.c source code for a very easy to
 * understand example. */
void linenoiseAddCompletion(linenoiseCompletions *lc, char *str, size_t pos) {
    size_t len = strlen(str);
    char *copy = malloc(len+1);
    memcpy(copy,str,len+1);
    if (lc->len == 0 || lc->cvec != NULL) {
        linenoiseSingleCompletion *newcvec = realloc(lc->cvec,sizeof(linenoiseSingleCompletion)*(lc->len+1));;
        if (newcvec != NULL) {
            lc->cvec = newcvec;
            lc->cvec[lc->len].text = copy;
            lc->cvec[lc->len].pos = pos;
        } else {
            free(lc->cvec);
            lc->cvec = NULL;
        }
    }
    if (len > lc->max_strlen)
        lc->max_strlen = len;
    lc->len++;
}

/* =========================== Line editing ================================= */

/* Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static int refreshSingleLine(struct linenoiseState *l) {
    char seq[64];
    size_t plen = l->plen;
    int fd = l->fd;
    char *buf = l->buf;
    size_t len = l->len;
    size_t pos = l->pos;
    
    while((plen+pos) >= l->cols) {
        buf++;
        len--;
        pos--;
    }
    while (plen+len > l->cols) {
        len--;
    }

    /* Cursor to left edge */
    if (snprintf(seq,64,"\x1b[0G") < 0) return -1;
    if (TEMP_FAILURE_RETRY(write(fd,seq,strlen(seq))) == -1) return -1;
    /* Write the prompt and the current buffer content */
    if (TEMP_FAILURE_RETRY(write(fd,l->prompt,l->plen)) == -1) return -1;
    if (TEMP_FAILURE_RETRY(write(fd,buf,len)) == -1) return -1;
    /* Erase to right */
    if (snprintf(seq,64,"\x1b[0K") < 0) return -1;
    if (TEMP_FAILURE_RETRY(write(fd,seq,strlen(seq))) == -1) return -1;
    /* Move cursor to original position. */
    if (pos+plen != 0) {
        if (snprintf(seq,64,"\x1b[0G\x1b[%dC", (int)(pos+plen)) < 0) return -1;
        if (TEMP_FAILURE_RETRY(write(fd,seq,strlen(seq))) == -1) return -1;
    }
    return 0;
}

/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static int refreshMultiLine(struct linenoiseState *l) {
    char seq[64];
    int plen = l->plen;
    int rows = (plen+l->len+l->cols-1)/l->cols; /* rows used by current buf. */
    int rpos = (plen+l->oldpos+l->cols)/l->cols; /* cursor relative row. */
    int rpos2; /* rpos after refresh. */
    int old_rows = l->maxrows;
    int fd = l->fd, j;

    /* Update maxrows if needed. */
    if (rows > (int)l->maxrows) l->maxrows = rows;

#ifdef LN_DEBUG
    FILE *fp = fopen("/tmp/debug.txt","a");
    fprintf(fp,"[%d %d %d] p: %d, rows: %d, rpos: %d, max: %d, oldmax: %d",
        (int)l->len,(int)l->pos,(int)l->oldpos,plen,rows,rpos,(int)l->maxrows,old_rows);
#endif

    /* First step: clear all the lines used before. To do so start by
     * going to the last row. */
    if (old_rows-rpos > 0) {
#ifdef LN_DEBUG
        fprintf(fp,", go down %d", old_rows-rpos);
#endif
        if (snprintf(seq,64,"\x1b[%dB", old_rows-rpos) < 0) return -1;
        if (TEMP_FAILURE_RETRY(write(fd,seq,strlen(seq))) == -1) return -1;
    }

    /* Now for every row clear it, go up. */
    for (j = 0; j < old_rows-1; j++) {
#ifdef LN_DEBUG
        fprintf(fp,", clear+up");
#endif
        if (snprintf(seq,64,"\x1b[0G\x1b[0K\x1b[1A") < 0) return -1;
        if (TEMP_FAILURE_RETRY(write(fd,seq,strlen(seq))) == -1) return -1;
    }

    /* Clean the top line. */
#ifdef LN_DEBUG
    fprintf(fp,", clear");
#endif
    if (snprintf(seq,64,"\x1b[0G\x1b[0K") < 0) return -1;
    if (TEMP_FAILURE_RETRY(write(fd,seq,strlen(seq))) == -1) return -1;
    
    /* Write the prompt and the current buffer content */
    if (TEMP_FAILURE_RETRY(write(fd,l->prompt,l->plen)) == -1) return -1;
    if (TEMP_FAILURE_RETRY(write(fd,l->buf,l->len)) == -1) return -1;

    /* If we are at the very end of the screen with our prompt, we need to
     * emit a newline and move the prompt to the first column. */
    if (l->pos &&
        l->pos == l->len &&
        (l->pos+plen) % l->cols == 0)
    {
#ifdef LN_DEBUG
        fprintf(fp,", <newline>");
#endif
        if (TEMP_FAILURE_RETRY(write(fd,"\n",1)) == -1) return -1;
        if (snprintf(seq,64,"\x1b[0G") < 0) return -1;
        if (TEMP_FAILURE_RETRY(write(fd,seq,strlen(seq))) == -1) return -1;
        rows++;
        if (rows > (int)l->maxrows) l->maxrows = rows;
    }

    /* Move cursor to right position. */
    rpos2 = (plen+l->pos+l->cols)/l->cols; /* current cursor relative row. */
#ifdef LN_DEBUG
    fprintf(fp,", rpos2 %d", rpos2);
#endif
    /* Go up till we reach the expected positon. */
    if (rows-rpos2 > 0) {
#ifdef LN_DEBUG
        fprintf(fp,", go-up %d", rows-rpos2);
#endif
        if (snprintf(seq,64,"\x1b[%dA", rows-rpos2) < 0) return -1;
        if (TEMP_FAILURE_RETRY(write(fd,seq,strlen(seq))) == -1) return -1;
    }
    /* Set column. */
#ifdef LN_DEBUG
    fprintf(fp,", set col %d", 1+((plen+(int)l->pos) % (int)l->cols));
#endif
    if (snprintf(seq,64,"\x1b[%dG", 1+((plen+(int)l->pos) % (int)l->cols)) < 0) return -1;
    if (write(fd,seq,strlen(seq)) == -1) return -1;

    l->oldpos = l->pos;

#ifdef LN_DEBUG
    fprintf(fp,"\n");
    fclose(fp);
#endif
    return 0;
}

/* Calls the two low level functions refreshSingleLine() or
 * refreshMultiLine() according to the selected mode. */
static int refreshLine(struct linenoiseState *l) {
    int result;
    if (mlmode)
        result = refreshMultiLine(l);
    else
        result = refreshSingleLine(l);
    if (result == 0) {
        l->needs_refresh = false;
        l->is_displayed = true;
    }
    return result;
}

static int prepareCustomOutput(struct linenoiseState *l)
{
    if (l->is_displayed) {
        struct linenoiseState oldstate = *l;
        l->pos = l->len;
        if (refreshLine(l) == -1) return -1;

        printf("\r\n");

        l->pos = oldstate.pos;
        l->maxrows = 0;

        l->needs_refresh = true;
        l->is_displayed = false;
    }
    return 0;
}

static int ensureBufLen(struct linenoiseState *l, size_t requestedStrLen)
{
    if (l->buflen < requestedStrLen) {
        size_t newlen = l->buflen + LINENOISE_LINE_INIT_MAX_AND_GROW;
        while (newlen < requestedStrLen)
            newlen += LINENOISE_LINE_INIT_MAX_AND_GROW;

        char *newbuf = realloc(l->buf, newlen);
        if (newbuf == NULL) {
            errno = ENOMEM;
            return -1;
        }
        l->buf = newbuf;
        l->buflen = newlen;
        l->buf[l->buflen-1] = '\0';
    }
    return 0;
}

/* Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. */
int linenoiseEditInsert(struct linenoiseState *l, int c) {
    if (l->len < l->buflen) {
        if (l->len == l->pos) {
            l->buf[l->pos] = c;
            l->pos++;
            l->len++;
            l->buf[l->len] = '\0';
            if ((!mlmode && l->plen+l->len < l->cols) /* || mlmode */) {
                /* Avoid a full update of the line in the
                 * trivial case. */
                if (TEMP_FAILURE_RETRY(write(l->fd,&c,1)) == -1) return -1;
            } else {
                if (refreshLine(l) == -1) return -1;
            }
        } else {
            memmove(l->buf+l->pos+1,l->buf+l->pos,l->len-l->pos);
            l->buf[l->pos] = c;
            l->len++;
            l->pos++;
            l->buf[l->len] = '\0';
            if (refreshLine(l) == -1) return -1;
        }
        return 0;
    } else {
        if (ensureBufLen(l, l->len + 1) == -1) return -1;
        return linenoiseEditInsert(l, c);
    }
}

int linenoiseEditCancel(struct linenoiseState *l)
{
    if (l->pos + 2 <= l->len) {
        l->buf[l->pos] = '^';
        l->buf[l->pos] = 'C';
        l->pos = l->len;
        if (refreshLine(l) == -1) return -1;
    } else if (l->pos + 1 == l->len) {
        l->buf[l->pos] = '^';
        l->pos = l->len;
        if (refreshLine(l) == -1) return -1;
        if (linenoiseEditInsert(l, 'C') == -1) return -1;
    } else {
        if (linenoiseEditInsert(l, '^') == -1) return -1;
        if (linenoiseEditInsert(l, 'C') == -1) return -1;
    }

    if (printf("\r\n") < 0) return -1;

    l->len = 0;
    l->maxrows = 0;

    l->needs_refresh = true;
    l->is_displayed = false;

    return 0;
}

/* Move cursor on the left. */
int linenoiseEditMoveLeft(struct linenoiseState *l) {
    if (l->pos > 0) {
        l->pos--;
        return refreshLine(l);
    }
    else
        return 0;
}

/* Move cursor on the right. */
int linenoiseEditMoveRight(struct linenoiseState *l) {
    if (l->pos != l->len) {
        l->pos++;
        return refreshLine(l);
    }
    else
        return 0;
}

/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1
int linenoiseEditHistoryNext(struct linenoiseState *l, int dir) {
    if (history_len > 1) {
        /* Update the current history entry before to
         * overwrite it with the next one. */
        free(history[history_len - 1 - l->history_index]);
        history[history_len - 1 - l->history_index] = strdup(l->buf);
        /* Show the new entry */
        l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
        if (l->history_index < 0) {
            l->history_index = 0;
            return 0;
        } else if (l->history_index >= history_len) {
            l->history_index = history_len-1;
            return 0;
        }
        size_t hist_strlen = strlen(history[history_len - 1 - l->history_index]);
        if (ensureBufLen(l, hist_strlen) == -1) return -1;
        memcpy(l->buf, history[history_len - 1 - l->history_index], hist_strlen+1);
        l->len = l->pos = strlen(l->buf);
        return refreshLine(l);
    }
    else
        return 0;
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
int linenoiseEditDelete(struct linenoiseState *l) {
    if (l->len > 0 && l->pos < l->len) {
        memmove(l->buf+l->pos,l->buf+l->pos+1,l->len-l->pos-1);
        l->len--;
        l->buf[l->len] = '\0';
        return refreshLine(l);
    } else return 0;
}

/* Backspace implementation. */
int linenoiseEditBackspace(struct linenoiseState *l) {
    if (l->pos > 0 && l->len > 0) {
        memmove(l->buf+l->pos-1,l->buf+l->pos,l->len-l->pos);
        l->pos--;
        l->len--;
        l->buf[l->len] = '\0';
        return refreshLine(l);
    } else return 0;
}

/* Delete the previosu word, maintaining the cursor at the start of the
 * current word. */
int linenoiseEditDeletePrevWord(struct linenoiseState *l) {
    size_t old_pos = l->pos;
    size_t diff;

    while (l->pos > 0 && l->buf[l->pos-1] == ' ')
        l->pos--;
    while (l->pos > 0 && l->buf[l->pos-1] != ' ')
        l->pos--;
    diff = old_pos - l->pos;
    memmove(l->buf+l->pos,l->buf+old_pos,l->len-old_pos+1);
    l->len -= diff;
    return refreshLine(l);
}

/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
static int linenoiseEdit(struct linenoiseState *l)
{
    if (l->needs_refresh || !l->is_displayed)
        if (refreshLine(l) == -1) return -1;

    /* Buffer starts empty. */
    l->buf[0] = '\0';

    /* The latest history entry is always our current buffer, that
     * initially is just an empty string. */
    if (l->state == LS_NEW_LINE) {
        linenoiseHistoryAdd("");
        l->state = LS_READ;
    }
    
    while(1) {
        char c;
        int nread;
        char seq[2], seq2[2];

        nread = read(l->fd,&c,1);

        if (nread < 0 && l->len == 0)
            return nread;
        if (nread <= 0)
            return l->len;

        /* Only autocomplete when the callback is set. It returns < 0 when
         * there was an error reading from fd. Otherwise it will return the
         * character that should be handled next. */
        if (c == 9 && completionCallback != NULL) {
            c = completeLine(l);
            /* Return on errors */
            if (c < 0) return l->len;
            /* Read next character when 0 */
            if (c == 0) continue;
        }

        switch(c) {
        case 13:    /* enter */
            l->state = LS_NEW_LINE;
            history_len--;
            free(history[history_len]);
            return (int)l->len;
        case 3:     /* ctrl-c */
            if (linenoiseEditCancel(l) == -1) return -1;
            errno = EAGAIN;
            return -1;
        case 127:   /* backspace */
        case 8:     /* ctrl-h */
            if (linenoiseEditBackspace(l) == -1) return -1;
            break;
        case 4:     /* ctrl-d, remove char at right of cursor, or of the
                       line is empty, act as end-of-file. */
            if (l->len > 0) {
                if (linenoiseEditDelete(l) == -1) return -1;
            } else {
                history_len--;
                free(history[history_len]);
                errno = 0;
                return 0;
            }
            break;
        case 20:    /* ctrl-t, swaps current character with previous. */
            if (l->pos > 0 && l->pos < l->len) {
                int aux = l->buf[l->pos-1];
                l->buf[l->pos-1] = l->buf[l->pos];
                l->buf[l->pos] = aux;
                if (l->pos != l->len-1) l->pos++;
                if (refreshLine(l) == -1) return -1;
            }
            break;
        case 2:     /* ctrl-b */
            if (linenoiseEditMoveLeft(l) == -1) return -1;
            break;
        case 6:     /* ctrl-f */
            if (linenoiseEditMoveRight(l) == -1) return -1;
            break;
        case 16:    /* ctrl-p */
            if (linenoiseEditHistoryNext(l, LINENOISE_HISTORY_PREV) == -1) return -1;
            break;
        case 14:    /* ctrl-n */
            if (linenoiseEditHistoryNext(l, LINENOISE_HISTORY_NEXT) == -1) return -1;
            break;
        case 27:    /* escape sequence */
            /* Read the next two bytes representing the escape sequence. */
            if (read(l->fd,seq,2) == -1) break;

            if (seq[0] == 91 && seq[1] == 68) {
                /* Left arrow */
                linenoiseEditMoveLeft(l);
            } else if (seq[0] == 91 && seq[1] == 67) {
                /* Right arrow */
                linenoiseEditMoveRight(l);
            } else if (seq[0] == 91 && (seq[1] == 65 || seq[1] == 66)) {
                /* Up and Down arrows */
                linenoiseEditHistoryNext(l,
                    (seq[1] == 65) ? LINENOISE_HISTORY_PREV :
                                     LINENOISE_HISTORY_NEXT);
            } else if (seq[0] == 91 && seq[1] > 48 && seq[1] < 55) {
                /* extended escape, read additional two bytes. */
                if (read(l->fd,seq2,2) == -1) break;
                if (seq[1] == 51 && seq2[0] == 126) {
                    /* Delete key. */
                    if (linenoiseEditDelete(l) == -1) return -1;
                }
            }
            break;
        default:
            if (linenoiseEditInsert(l,c) == -1) return -1;
            break;
        case 21: /* Ctrl+u, delete the whole line. */
            l->buf[0] = '\0';
            l->pos = l->len = 0;
            if (refreshLine(l) == -1) return -1;
            break;
        case 11: /* Ctrl+k, delete from current to end of line. */
            l->buf[l->pos] = '\0';
            l->len = l->pos;
            if (refreshLine(l) == -1) return -1;
            break;
        case 1: /* Ctrl+a, go to the start of the line */
            l->pos = 0;
            if (refreshLine(l) == -1) return -1;
            break;
        case 5: /* ctrl+e, go to the end of the line */
            l->pos = l->len;
            if (refreshLine(l) == -1) return -1;
            break;
        case 12: /* ctrl+l, clear screen */
            if (linenoiseClearScreen() == -1) return -1;
            if (refreshLine(l) == -1) return -1;
            break;
        case 23: /* ctrl+w, delete previous word */
            if (linenoiseEditDeletePrevWord(l) == -1) return -1;
            break;
        }
    }
    return l->len;
}

/* This function calls the line editing function linenoiseEdit() using
 * the STDIN file descriptor set in raw mode. */
static int linenoiseRaw(struct linenoiseState *l) {
    int count = 1;

    if (l->buflen == 0) {
        errno = EINVAL;
        return -1;
    }
    if (enableRawMode(l->fd) == -1) return -1;

    count = linenoiseEdit(l);

    int savederrno = errno;
    disableRawMode(l->fd);

    errno = savederrno;
    return count;
}

/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
char *linenoise(const char *prompt) {
    if (isUnsupportedTerm()) {
        char buf[LINENOISE_LINE_INIT_MAX_AND_GROW];
        size_t len;

        printf("%s",prompt);
        fflush(stdout);
        if (fgets(buf,LINENOISE_LINE_INIT_MAX_AND_GROW,stdin) == NULL) return NULL;
        len = strlen(buf);
        while(len && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            len--;
            buf[len] = '\0';
        }
        return strdup(buf);
    } else {
        int count;

        errno = 0;
        if ( !initialized )
        {
            if (initialize(prompt) == -1) return NULL;
            initialized = true;
        }
        else
        {
            size_t newCols = getColumns();
            if ( newCols != state.cols )
            {
                state.cols = newCols;
                state.needs_refresh = true;
            }
            if (strcmp(state.prompt, prompt) != 0) {
                free(state.prompt);
                state.prompt = strdup(prompt);
                state.plen = strlen(prompt);
                state.needs_refresh = true;
                if (state.prompt == NULL) return NULL;
            }
        }

        count = linenoiseRaw(&state);
        if (count == -1) return NULL;

        char* result = strdup(state.buf);
        if (result == NULL) errno = ENOMEM;

        if (count >= 0) {
            printf("\r\n");
            state.maxrows = 0;
            state.pos = state.len = 0;
            state.buf[0] = '\0';
            state.needs_refresh = true;
            state.is_displayed = false;
        }

        return result;
    }
}

static int initialize(const char *prompt)
{
    atexit(linenoiseAtExit);

    /* Populate the linenoise state that we pass to functions implementing
     * specific editing functionalities. */
    state.fd = STDIN_FILENO;
    state.buf = malloc(LINENOISE_LINE_INIT_MAX_AND_GROW);
    state.buflen = LINENOISE_LINE_INIT_MAX_AND_GROW;
    state.prompt = strdup(prompt);
    state.plen = strlen(prompt);
    state.oldpos = state.pos = 0;
    state.len = 0;
    state.cols = getColumns();
    state.maxrows = 0;
    state.history_index = 0;
    state.needs_refresh = true;
    state.state = LS_NEW_LINE;
    if ( state.buf == NULL || state.prompt == NULL )
    {
        errno = ENOMEM;
        return -1;
    }
    state.buf[0] = '\0';
    return 0;
}

static void freeState()
{
    free(state.prompt);
    free(state.buf);
}

/* ================================ History ================================= */

/* Free the history, but does not reset it. Only used when we have to
 * exit() to avoid memory leaks are reported by valgrind & co. */
static void freeHistory(void) {
    if (history) {
        int j;

        for (j = 0; j < history_len; j++)
            free(history[j]);
        free(history);
    }
}

/* At exit we'll try to fix the terminal to the initial conditions. */
static void linenoiseAtExit(void) {
    disableRawMode(STDIN_FILENO);
    freeHistory();
    freeState();
}

/* Using a circular buffer is smarter, but a bit more complex to handle. */
int linenoiseHistoryAdd(const char *line) {
    char *linecopy;

    if (history_max_len == 0) return 0;
    if (history == NULL) {
        history = malloc(sizeof(char*)*history_max_len);
        if (history == NULL) return 0;
        memset(history,0,(sizeof(char*)*history_max_len));
    }
    linecopy = strdup(line);
    if (!linecopy) return 0;
    if (history_len == history_max_len) {
        free(history[0]);
        memmove(history,history+1,sizeof(char*)*(history_max_len-1));
        history_len--;
    }
    history[history_len] = linecopy;
    history_len++;
    return 1;
}

/* Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. */
int linenoiseHistorySetMaxLen(int len) {
    char **new;

    if (len < 1) return 0;
    if (history) {
        int tocopy = history_len;

        new = malloc(sizeof(char*)*len);
        if (new == NULL) return 0;

        /* If we can't copy everything, free the elements we'll not use. */
        if (len < tocopy) {
            int j;

            for (j = 0; j < tocopy-len; j++) free(history[j]);
            tocopy = len;
        }
        memset(new,0,sizeof(char*)*len);
        memcpy(new,history+(history_len-tocopy), sizeof(char*)*tocopy);
        free(history);
        history = new;
    }
    history_max_len = len;
    if (history_len > history_max_len)
        history_len = history_max_len;
    return 1;
}

/* Save the history in the specified file. On success 0 is returned
 * otherwise -1 is returned. */
int linenoiseHistorySave(char *filename) {
    FILE *fp = fopen(filename,"w");
    int j;
    
    if (fp == NULL) return -1;
    for (j = 0; j < history_len; j++)
        fprintf(fp,"%s\n",history[j]);
    fclose(fp);
    return 0;
}

/* Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int linenoiseHistoryLoad(char *filename) {
    FILE *fp = fopen(filename,"r");
    char buf[LINENOISE_LINE_INIT_MAX_AND_GROW];
    
    if (fp == NULL) return -1;

    while (fgets(buf,LINENOISE_LINE_INIT_MAX_AND_GROW,fp) != NULL) {
        char *p;
        
        p = strchr(buf,'\r');
        if (!p) p = strchr(buf,'\n');
        if (p) *p = '\0';
        linenoiseHistoryAdd(buf);
    }
    fclose(fp);
    return 0;
}
