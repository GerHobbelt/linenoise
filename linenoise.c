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
 * Copyright (c) 2010-2016, Salvatore Sanfilippo <antirez at gmail dot com>
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
 * EL (Erase Line)
 *    Sequence: ESC [ n K
 *    Effect: if n is 0 or missing, clear from cursor to end of line
 *    Effect: if n is 1, clear from beginning of line to cursor
 *    Effect: if n is 2, clear entire line
 *
 * CUF (CUrsor Forward)
 *    Sequence: ESC [ n C
 *    Effect: moves cursor forward n chars
 *
 * CUB (CUrsor Backward)
 *    Sequence: ESC [ n D
 *    Effect: moves cursor backward n chars
 *
 * The following is used to get the terminal width if getting
 * the width with the TIOCGWINSZ ioctl fails
 *
 * DSR (Device Status Report)
 *    Sequence: ESC [ 6 n
 *    Effect: reports the current cusor position as ESC [ n ; m R
 *            where n is the row and m is the column
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
 * When linenoiseClearScreen() is called, two additional escape sequences
 * are used in order to clear the screen and position the cursor at home
 * position.
 *
 * CUP (Cursor position)
 *    Sequence: ESC [ H
 *    Effect: moves the cursor to upper left corner
 *
 * ED (Erase display)
 *    Sequence: ESC [ 2 J
 *    Effect: clear the whole screen
 *
 */

#ifndef __riscos
#include <termios.h>
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#ifndef __riscos
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include "linenoise.h"

#ifdef __riscos
#include "ro_cursors.h"
// Terminal status - we'll use this to hold the configuration of the cursor keys.
struct termios {
    cursorstate_t cursorstate;
};
#endif

#ifdef __riscos
#include "swis.h"
char *strdup(const char *s)
{
    int len = strlen(s);
    char *p = malloc(len + 1);
    if (p)
        memcpy(p, s, len + 1);
    return p;
}

int os_readc(char *p, int len)
{
    /* Call OS_ReadC and return the 1 byte */
    if (_swix(OS_ReadC, _OUT(0), p))
    {
        *p = 0;
        return 0;
    }
    /* Return number of bytes read. If they pressed escape return 0 bytes */
    return 1;
}
int os_writen(const char *p, int len)
{
    _swix(OS_WriteN, _INR(0, 1), p, len);
    return len;
}
#define read(fh, p, len) (os_readc)(p, len)
#define write(fh, p, len) (os_writen)(p, len)

#define STDIN_FILENO (0)
#define STDOUT_FILENO (1)
#define EAGAIN (7)
#define EINVAL (9)

#define VduVariable_WindowWidth (0x100)
#define OSByte_ReadTextOutputCursor (0xA5)
#endif

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096

#ifndef __riscos
static char *unsupported_term[] = {"dumb","cons25","emacs",NULL};
#endif

#ifndef __riscos
static struct termios orig_termios; /* In order to restore at exit.*/
static int rawmode = 0; /* For atexit() function to check if restore is needed*/
static int atexit_registered = 0; /* Register atexit just 1 time. */
#endif

struct linenoiseConfig {
    int history_max_len;
    int history_len;
    char **history;

    int mlmode;
    int maskmode;

    linenoiseCompletionCallback *completionCallback;
    linenoiseHintsCallback      *hintsCallback;
    linenoiseFreeHintsCallback  *freeHintsCallback;
};

static struct linenoiseConfig default_config = {
    LINENOISE_DEFAULT_HISTORY_MAX_LEN,
    0,
    NULL,

    0,
    LINENOISE_MASKMODE_DISABLED,
    NULL,
    NULL,
    NULL,
};
static struct linenoiseConfig linenoiseGlobalConfig = {
    LINENOISE_DEFAULT_HISTORY_MAX_LEN,
    0,
    NULL,

    0,
    LINENOISE_MASKMODE_DISABLED,
    NULL,
    NULL,
    NULL,
};

/* The linenoiseState structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct linenoiseState {
    int ifd;            /* Terminal stdin file descriptor. */
    int ofd;            /* Terminal stdout file descriptor. */
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
    int hintsdisabled;  /* Whether we've disabled the hints for this refresh. */

    struct linenoiseConfig *config; /* Configuration for this invocation */

#ifdef __riscos
    cursorstate_t cursorstate;
#endif
};

enum KEY_ACTION{
	KEY_NULL = 0,	    /* NULL */
	CTRL_A = 1,         /* Ctrl+a */
	CTRL_B = 2,         /* Ctrl-b */
	CTRL_C = 3,         /* Ctrl-c */
	CTRL_D = 4,         /* Ctrl-d */
	CTRL_E = 5,         /* Ctrl-e */
	CTRL_F = 6,         /* Ctrl-f */
	CTRL_H = 8,         /* Ctrl-h */
	TAB = 9,            /* Tab */
	CTRL_K = 11,        /* Ctrl+k */
	CTRL_L = 12,        /* Ctrl+l */
	ENTER = 13,         /* Enter */
	CTRL_N = 14,        /* Ctrl-n */
	CTRL_P = 16,        /* Ctrl-p */
	CTRL_T = 20,        /* Ctrl-t */
	CTRL_U = 21,        /* Ctrl+u */
	CTRL_W = 23,        /* Ctrl+w */
	ESC = 27,           /* Escape */
	BACKSPACE =  127    /* Backspace */
};

enum CURSORS {
    CURSOR_LEFT = 136,
    CURSOR_RIGHT = 137,
    CURSOR_DOWN = 138,
    CURSOR_UP = 139
};

static void linenoiseAtExit(void);
int linenoiseHistoryAdd(const char *line);
static void refreshLine(struct linenoiseState *l);
static void freeHistory(struct linenoiseConfig *config);

/* Debugging macro. */
#if 0
FILE *lndebug_fp = NULL;
#define lndebug(...) \
    do { \
        if (lndebug_fp == NULL) { \
            lndebug_fp = fopen("/tmp/lndebug.txt","a"); \
            fprintf(lndebug_fp, \
            "[%d %d %d] p: %d, rows: %d, rpos: %d, max: %d, oldmax: %d\n", \
            (int)l->len,(int)l->pos,(int)l->oldpos,plen,rows,rpos, \
            (int)l->maxrows,old_rows); \
        } \
        fprintf(lndebug_fp, ", " __VA_ARGS__); \
        fflush(lndebug_fp); \
    } while (0)
#else
#ifdef __riscos
    #define lndebug if (0) printf
#else
    #define lndebug(fmt, ...)
#endif
#endif


/* ======================= Configuration ====================== */

struct linenoiseConfig *linenoiseNewConfig(void)
{
    struct linenoiseConfig *config = malloc(sizeof(struct linenoiseConfig));
    if (config == NULL)
        return NULL;

    *config = default_config;
    return config;
}

void linenoiseFreeConfig(struct linenoiseConfig *config)
{
    if (config != NULL)
    {
        freeHistory(config);
        linenoiseFree(config);
    }
}

void linenoiseConfigSetMultiLine(struct linenoiseConfig *config, int ml)
{
    config->mlmode = ml;
}
void linenoiseConfigSetMaskMode(struct linenoiseConfig *config, int maskmode)
{
    config->maskmode = maskmode;
}


/* Enable "mask mode". When it is enabled, instead of the input that
 * the user is typing, the terminal will just display a corresponding
 * number of asterisks, like "****". This is useful for passwords and other
 * secrets that should not be displayed. */
void linenoiseMaskModeEnable(void) {
    linenoiseConfigSetMaskMode(&linenoiseGlobalConfig, LINENOISE_MASKMODE_ENABLED);
}

void linenoiseMaskModeChar(char c) {
    linenoiseConfigSetMaskMode(&linenoiseGlobalConfig, (int)c);
}

/* Disable mask mode. */
void linenoiseMaskModeDisable(void) {
    linenoiseConfigSetMaskMode(&linenoiseGlobalConfig, LINENOISE_MASKMODE_DISABLED);
}

/* Set if to use or not the multi line mode. */
void linenoiseSetMultiLine(int ml) {
    linenoiseConfigSetMultiLine(&linenoiseGlobalConfig, ml);
}


/* ======================= Low level terminal handling ====================== */

/* Return true if the terminal name is in the list of terminals we know are
 * not able to understand basic escape sequences. */
static int isUnsupportedTerm(void) {
#ifdef __riscos
    return 0; /* We're built for RISC OS, so we'll understand the system */
#else
    char *term = getenv("TERM");
    int j;

    if (term == NULL) return 0;
    for (j = 0; unsupported_term[j]; j++)
        if (!strcasecmp(term,unsupported_term[j])) return 1;
    return 0;
#endif
}

/* Raw mode: 1960 magic shit. */
static int enableRawMode(int fd) {
#ifdef __riscos
    return 0;
#else
    struct termios raw;

    if (!isatty(STDIN_FILENO)) goto fatal;
    if (!atexit_registered) {
        atexit(linenoiseAtExit);
        atexit_registered = 1;
    }
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
#endif
}

static void disableRawMode(int fd) {
#ifdef __riscos
    /* Restore back to the original cursor configuration */
#else
    /* Don't even check the return value as it's too late. */
    if (rawmode && tcsetattr(fd,TCSAFLUSH,&orig_termios) != -1)
        rawmode = 0;
#endif
}

/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor. */
static int getCursorPosition(int ifd, int ofd) {
#ifdef __riscos
    /* Read the cursor position using OS_ReadVduVariables? */
    int cols;
    _kernel_oserror *err = _swix(OS_Byte, _IN(0)|_OUT(0), OSByte_ReadTextOutputCursor, &cols);
    if (err)
        return -1;
#else
    char buf[32];
    int cols, rows;
    unsigned int i = 0;

    /* Report cursor location */
    if (write(ofd, "\x1b[6n", 4) != 4) return -1;

    /* Read the response: ESC [ rows ; cols R */
    while (i < sizeof(buf)-1) {
        if (read(ifd,buf+i,1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    /* Parse it. */
    if (buf[0] != ESC || buf[1] != '[') return -1;
    if (sscanf(buf+2,"%d;%d",&rows,&cols) != 2) return -1;
#endif
    return cols;
}

/* Try to get the number of columns in the current terminal, or assume 80
 * if it fails. */
static int getColumns(int ifd, int ofd) {
#ifdef __riscos
    /* Read the cursor position using OS_ReadVduVariables? */
    static const int vars[] = {
            VduVariable_WindowWidth,
            -1
        };
    static const int vals[1];
    _kernel_oserror *err;
    err = _swix(OS_ReadVduVariables, _INR(0, 1), &vars, &vals);
    if (err)
        goto failed;

    return vals[0];
#else
    struct winsize ws;

    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        /* ioctl() failed. Try to query the terminal itself. */
        int start, cols;

        /* Get the initial position so we can restore it later. */
        start = getCursorPosition(ifd,ofd);
        if (start == -1) goto failed;

        /* Go to right margin and get position. */
        if (write(ofd,"\x1b[999C",6) != 6) goto failed;
        cols = getCursorPosition(ifd,ofd);
        if (cols == -1) goto failed;

        /* Restore position. */
        if (cols > start) {
            char seq[32];
            snprintf(seq,32,"\x1b[%dD",cols-start);
            if (write(ofd,seq,strlen(seq)) == -1) {
                /* Can't recover... */
            }
        }
        return cols;
    } else {
        return ws.ws_col;
    }
#endif

failed:
    return 80;
}

/* Clear the screen. Used to handle ctrl+l */
void linenoiseClearScreen(void) {
#ifdef __riscos
    /* VDU 12? */
    write(STDOUT_FILENO, "\x0c", 1);
#else
    if (write(STDOUT_FILENO,"\x1b[H\x1b[2J",7) <= 0) {
        /* nothing to do, just to avoid warning. */
    }
#endif
}

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
static void linenoiseBeep(void) {
#ifdef __riscos
    /* VDU 7 to beep */
    write(STDOUT_FILENO, "\x07", 1);
#else
    fprintf(stderr, "\x7");
    fflush(stderr);
#endif
}

/* ============================== Completion ================================ */

/* Free a list of completion option populated by linenoiseAddCompletion(). */
static void freeCompletions(linenoiseCompletions *lc) {
    size_t i;
    for (i = 0; i < lc->len; i++)
        free(lc->cvec[i]);
    if (lc->cvec != NULL)
        free(lc->cvec);
}

/* This is an helper function for linenoiseEdit() and is called when the
 * user types the <tab> key in order to complete the string currently in the
 * input.
 *
 * The state of the editing is encapsulated into the pointed linenoiseState
 * structure as described in the structure definition. */
static int completeLine(struct linenoiseState *ls) {
    linenoiseCompletions lc = { 0, NULL };
    int nread, nwritten;
    char c = 0;

    if (ls->config->completionCallback)
        ls->config->completionCallback(ls->buf,&lc);

    if (lc.len == 0) {
        linenoiseBeep();
    } else {
        size_t stop = 0, i = 0;

        while(!stop) {
            /* Show completion or original buffer */
            if (i < lc.len) {
                struct linenoiseState saved = *ls;

                ls->len = ls->pos = strlen(lc.cvec[i]);
                ls->buf = lc.cvec[i];
                refreshLine(ls);
                ls->len = saved.len;
                ls->pos = saved.pos;
                ls->buf = saved.buf;
            } else {
                refreshLine(ls);
            }

            nread = read(ls->ifd,&c,1);
            if (nread <= 0) {
                freeCompletions(&lc);
                return -1;
            }

            switch(c) {
                case 9: /* tab */
                    i = (i+1) % (lc.len+1);
                    if (i == lc.len) linenoiseBeep();
                    break;
                case 27: /* escape */
                    /* Re-show original buffer */
                    if (i < lc.len) refreshLine(ls);
                    stop = 1;
                    break;
                default:
                    /* Update buffer and return */
                    if (i < lc.len) {
#ifdef __riscos
                        int len = strlen(lc.cvec[i]);
                        nwritten = (len > ls->buflen - 1) ? ls->buflen - 1 : len;
                        memcpy(ls->buf, lc.cvec[i], nwritten);
                        ls->buf[nwritten] = '\0';
#else
                        nwritten = snprintf(ls->buf,ls->buflen,"%s",lc.cvec[i]);
#endif
                        ls->len = ls->pos = nwritten;
                    }
                    stop = 1;
                    break;
            }
        }
    }

    freeCompletions(&lc);
    return c; /* Return last read character */
}

/* Register a callback function to be called for tab-completion. */
void linenoiseConfigSetCompletionCallback(struct linenoiseConfig *config, linenoiseCompletionCallback *fn) {
    if (config == NULL)
        config = &linenoiseGlobalConfig;

    config->completionCallback = fn;
}

/* Register a hits function to be called to show hits to the user at the
 * right of the prompt. */
void linenoiseConfigSetHintsCallback(struct linenoiseConfig *config, linenoiseHintsCallback *fn) {
    if (config == NULL)
        config = &linenoiseGlobalConfig;

    config->hintsCallback = fn;
}

/* Register a function to free the hints returned by the hints callback
 * registered with linenoiseSetHintsCallback(). */
void linenoiseConfigSetFreeHintsCallback(struct linenoiseConfig *config, linenoiseFreeHintsCallback *fn) {
    if (config == NULL)
        config = &linenoiseGlobalConfig;

    config->freeHintsCallback = fn;
}

/* This function is used by the callback function registered by the user
 * in order to add completion options given the input string when the
 * user typed <tab>. See the example.c source code for a very easy to
 * understand example. */
void linenoiseAddCompletion(linenoiseCompletions *lc, const char *str) {
    size_t len = strlen(str);
    char *copy, **cvec;

    copy = malloc(len+1);
    if (copy == NULL) return;
    memcpy(copy,str,len+1);
    cvec = realloc(lc->cvec,sizeof(char*)*(lc->len+1));
    if (cvec == NULL) {
        free(copy);
        return;
    }
    lc->cvec = cvec;
    lc->cvec[lc->len++] = copy;
}

/* =========================== Line editing ================================= */

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf {
    char *b;
    int len;
};

static void abInit(struct abuf *ab) {
    ab->b = NULL;
    ab->len = 0;
}

static void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b,ab->len+len);

    if (new == NULL) return;
    memcpy(new+ab->len,s,len);
    ab->b = new;
    ab->len += len;
}

static void abFree(struct abuf *ab) {
    free(ab->b);
}

/* Helper of refreshSingleLine() and refreshMultiLine() to show hints
 * to the right of the prompt. */
void refreshShowHints(struct abuf *ab, struct linenoiseState *l, int plen) {
    char seq[64];
    if (l->config->hintsCallback && !l->hintsdisabled && plen+l->len < l->cols) {
        int color = -1, bold = 0;
        char *hint = l->config->hintsCallback(l->buf,&color,&bold);
        if (hint) {
            int hintlen = strlen(hint);
            int hintmaxlen = l->cols-(plen+l->len);
            if (hintlen > hintmaxlen) hintlen = hintmaxlen;
            if (bold == 1 && color == -1) color = 37;
            if (color != -1 || bold != 0)
            {
#ifdef __riscos
                /* Cannot be more than 64 bytes: 2+9+1+9+4+1 < 64 */
                /* FIXME: Select bold and colour in RISC OS - ColourTrans?*/
                strcpy(seq, "");
#else
                snprintf(seq,64,"\033[%d;%d;49m",bold,color);
#endif
            }
            else
                seq[0] = '\0';
            abAppend(ab,seq,strlen(seq));
            abAppend(ab,hint,hintlen);
            if (color != -1 || bold != 0)
            {
#ifdef __riscos
                /* Restore colours */
#else
                abAppend(ab,"\033[0m",4);
#endif
            }
            /* Call the function to free the hint returned. */
            if (l->config->freeHintsCallback)
                l->config->freeHintsCallback(hint);
        }
    }
}

/* Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void refreshSingleLine(struct linenoiseState *l) {
    char seq[64];
    size_t plen = strlen(l->prompt);
    int fd = l->ofd;
    char *buf = l->buf;
    size_t len = l->len;
    size_t pos = l->pos;
    struct abuf ab;

    while((plen+pos) >= l->cols) {
        buf++;
        len--;
        pos--;
    }
    while (plen+len > l->cols) {
        len--;
    }

    abInit(&ab);
    /* Cursor to left edge */
#ifdef __riscos
    /* Cannot be longer than 64 bytes: 1+1 < 64 */
    sprintf(seq,"\r");
#else
    snprintf(seq,64,"\r");
#endif
    abAppend(&ab,seq,strlen(seq));
    /* Write the prompt and the current buffer content */
    abAppend(&ab,l->prompt,plen);
    if (l->config->maskmode != LINENOISE_MASKMODE_DISABLED) {
        while (len--) abAppend(&ab, (char*)&l->config->maskmode, 1);
    } else {
        abAppend(&ab,buf,len);
    }
    /* Show hits if any. */
    refreshShowHints(&ab,l,plen);
    /* Erase to right */
#ifdef __riscos
    /* Erase to right is not implemented in Pyromaniac, so instead we'll
     * just overwrite them all. */
    {
        int offset = plen + len;
        int spaces = l->cols - (offset % l->cols) - 1;
        for (; spaces; spaces--)
            abAppend(&ab," ",1);
        abAppend(&ab, "\r", 1); /* Move to start of line */
    }
#else
    snprintf(seq,64,"\x1b[0K");
    abAppend(&ab,seq,strlen(seq));
#endif
    /* Move cursor to original position. */
#ifdef __riscos
    {
        int i;
        for (i=(int)(pos+plen); i; i--)
            abAppend(&ab, "\x09", 1); /* Move right one char */
    }
#else
    snprintf(seq,64,"\r\x1b[%dC", (int)(pos+plen));
    abAppend(&ab,seq,strlen(seq));
#endif
    if (write(fd,ab.b,ab.len) == -1) {} /* Can't recover from write error. */
    abFree(&ab);
}

/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void refreshMultiLine(struct linenoiseState *l) {
    char seq[64];
    int plen = strlen(l->prompt);
    int rows = (plen+l->len+l->cols-1)/l->cols; /* rows used by current buf. */
    int rpos = (plen+l->oldpos+l->cols)/l->cols; /* cursor relative row. */
    int rpos2; /* rpos after refresh. */
    int col; /* colum position, zero-based. */
    int old_rows = l->maxrows;
    int fd = l->ofd, j;
    struct abuf ab;

    /* Update maxrows if needed. */
    if (rows > (int)l->maxrows) l->maxrows = rows;

    /* First step: clear all the lines used before. To do so start by
     * going to the last row. */
    abInit(&ab);
    if (old_rows-rpos > 0) {
        lndebug("go down %d", old_rows-rpos);
#ifdef __riscos
        {
            int lines;
            for (lines = old_rows-rpos; lines; lines--)
                abAppend(&ab,"\n",1);
        }
#else
        snprintf(seq,64,"\x1b[%dB", old_rows-rpos);
#endif
        abAppend(&ab,seq,strlen(seq));
    }

    /* Now for every row clear it, go up. */
    for (j = 0; j < old_rows-1; j++) {
        lndebug("clear+up");
#ifdef __riscos
        /* Clear to end of line is not supported, so we're going t0
         * do this the hard way.
         */
        {
            int spaces;
            abAppend(&ab,"\r",1);
            for (spaces = l->cols; spaces; spaces--)
                abAppend(&ab," ",1);
            for (spaces = l->cols; spaces; spaces--)
                abAppend(&ab,"\x08",1);
            abAppend(&ab, "\x0b", 1); /* Move up a line */
        }
#else
        snprintf(seq,64,"\r\x1b[0K\x1b[1A");
        abAppend(&ab,seq,strlen(seq));
#endif
    }
/* hello there this is a test of the long lines to see what happens when you write a lot of text. */
    /* Clean the top line. */
    lndebug("clear");
#ifdef __riscos
    /* Erase to right is not implemented in Pyromaniac, so instead we'll
     * just overwrite them all. */
    {
        int spaces;
        abAppend(&ab, "\r", 1); /* Move to start of line */
        for (spaces = l->cols - 1; spaces; spaces--)
            abAppend(&ab," ",1);
        for (spaces = l->cols; spaces; spaces--)
            abAppend(&ab,"\x08",1);
        abAppend(&ab, "\r", 1); /* Move back to start of line */
    }
#else
    snprintf(seq,64,"\r\x1b[0K");
    abAppend(&ab,seq,strlen(seq));
#endif

    /* Write the prompt and the current buffer content */
    abAppend(&ab,l->prompt,strlen(l->prompt));
    if (l->config->maskmode != LINENOISE_MASKMODE_DISABLED) {
        unsigned int i;
        for (i = 0; i < l->len; i++)
            abAppend(&ab, (char*)&l->config->maskmode, 1);
    } else {
        abAppend(&ab,l->buf,l->len);
    }

    /* Show hits if any. */
    refreshShowHints(&ab,l,plen);

    /* If we are at the very end of the screen with our prompt, we need to
     * emit a newline and move the prompt to the first column. */
    if (l->pos &&
        l->pos == l->len &&
        (l->pos+plen) % l->cols == 0)
    {
        lndebug("<newline>");
        abAppend(&ab,"\n",1);
#ifdef __riscos
        /* Less than 64 bytes: 1+1 < 64 */
        sprintf(seq,"\r");
#else
        snprintf(seq,64,"\r");
#endif
        abAppend(&ab,seq,strlen(seq));
        rows++;
        if (rows > (int)l->maxrows) l->maxrows = rows;
    }

    /* Move cursor to right position. */
    rpos2 = (plen+l->pos+l->cols)/l->cols; /* current cursor relative row. */
    lndebug("rpos2 %d", rpos2);

    /* Go up till we reach the expected positon. */
    if (rows-rpos2 > 0) {
        lndebug("go-up %d", rows-rpos2);
#ifdef __riscos
        {
            int lines;
            for (lines = rows-rpos2; lines; lines--)
                abAppend(&ab,"\n",1);
        }
#else
        snprintf(seq,64,"\x1b[%dA", rows-rpos2);
        abAppend(&ab,seq,strlen(seq));
#endif
    }

    /* Set column. */
    col = (plen+(int)l->pos) % (int)l->cols;
    lndebug("set col %d", 1+col);
    if (col)
    {
#ifdef __riscos
        int i;
        abAppend(&ab,"\r",1);
        for (i=col; i; i--)
            abAppend(&ab, "\x09", 1); /* Move right one char */
#else
        snprintf(seq,64,"\r\x1b[%dC", col);
        abAppend(&ab,seq,strlen(seq));
#endif
    }
    else
    {
#ifdef __riscos
        sprintf(seq,"\r");
#else
        snprintf(seq,64,"\r");
#endif
        abAppend(&ab,seq,strlen(seq));
    }

    lndebug("\n");
    l->oldpos = l->pos;

    if (write(fd,ab.b,ab.len) == -1) {} /* Can't recover from write error. */
    abFree(&ab);
}

/* Calls the two low level functions refreshSingleLine() or
 * refreshMultiLine() according to the selected mode. */
static void refreshLine(struct linenoiseState *l) {
    if (l->config->mlmode)
        refreshMultiLine(l);
    else
        refreshSingleLine(l);
}

/* Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. */
int linenoiseEditInsert(struct linenoiseState *l, char c) {
    if (l->len < l->buflen) {
        if (l->len == l->pos) {
            l->buf[l->pos] = c;
            l->pos++;
            l->len++;
            l->buf[l->len] = '\0';
            if ((!l->config->mlmode && l->plen+l->len < l->cols && (!l->config->hintsCallback || l->hintsdisabled))) {
                /* Avoid a full update of the line in the
                 * trivial case. */
                char d = (l->config->maskmode!=LINENOISE_MASKMODE_DISABLED) ? l->config->maskmode : c;
                if (write(l->ofd,&d,1) == -1) return -1;
            } else {
                refreshLine(l);
            }
        } else {
            memmove(l->buf+l->pos+1,l->buf+l->pos,l->len-l->pos);
            l->buf[l->pos] = c;
            l->len++;
            l->pos++;
            l->buf[l->len] = '\0';
            refreshLine(l);
        }
    }
    return 0;
}

/* Move cursor on the left. */
void linenoiseEditMoveLeft(struct linenoiseState *l) {
    if (l->pos > 0) {
        l->pos--;
        refreshLine(l);
    }
}

/* Move cursor on the right. */
void linenoiseEditMoveRight(struct linenoiseState *l) {
    if (l->pos != l->len) {
        l->pos++;
        refreshLine(l);
    }
}

/* Move cursor to the start of the line. */
void linenoiseEditMoveHome(struct linenoiseState *l) {
    if (l->pos != 0) {
        l->pos = 0;
        refreshLine(l);
    }
}

/* Move cursor to the end of the line. */
void linenoiseEditMoveEnd(struct linenoiseState *l) {
    if (l->pos != l->len) {
        l->pos = l->len;
        refreshLine(l);
    }
}

/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1
void linenoiseEditHistoryNext(struct linenoiseState *l, int dir) {
    if (l->config->history_len > 1) {
        /* Update the current history entry before to
         * overwrite it with the next one. */
        free(l->config->history[l->config->history_len - 1 - l->history_index]);
        l->config->history[l->config->history_len - 1 - l->history_index] = strdup(l->buf);
        /* Show the new entry */
        l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
        if (l->history_index < 0) {
            l->history_index = 0;
            return;
        } else if (l->history_index >= l->config->history_len) {
            l->history_index = l->config->history_len-1;
            return;
        }
        strncpy(l->buf, l->config->history[l->config->history_len - 1 - l->history_index],l->buflen);
        l->buf[l->buflen-1] = '\0';
        l->len = l->pos = strlen(l->buf);
        refreshLine(l);
    }
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
void linenoiseEditDelete(struct linenoiseState *l) {
    if (l->len > 0 && l->pos < l->len) {
        memmove(l->buf+l->pos,l->buf+l->pos+1,l->len-l->pos-1);
        l->len--;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

/* Backspace implementation. */
void linenoiseEditBackspace(struct linenoiseState *l) {
    if (l->pos > 0 && l->len > 0) {
        memmove(l->buf+l->pos-1,l->buf+l->pos,l->len-l->pos);
        l->pos--;
        l->len--;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

/* Delete the previosu word, maintaining the cursor at the start of the
 * current word. */
void linenoiseEditDeletePrevWord(struct linenoiseState *l) {
    size_t old_pos = l->pos;
    size_t diff;

    while (l->pos > 0 && l->buf[l->pos-1] == ' ')
        l->pos--;
    while (l->pos > 0 && l->buf[l->pos-1] != ' ')
        l->pos--;
    diff = old_pos - l->pos;
    memmove(l->buf+l->pos,l->buf+old_pos,l->len-old_pos+1);
    l->len -= diff;
    refreshLine(l);
}

/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
static int linenoiseEdit(struct linenoiseConfig *config, int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt)
{
    struct linenoiseState l;

    if (config == NULL)
        config = &linenoiseGlobalConfig;

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
    l.cols = getColumns(stdin_fd, stdout_fd);
    l.maxrows = 0;
    l.history_index = 0;
    l.hintsdisabled = 0;
    l.config = config;

#ifdef __riscos
    cursors_readstate(&l.cursorstate);
    cursors_keys(&l.cursorstate, 1);
#endif

    /* Buffer starts empty. */
    l.buf[0] = '\0';
    l.buflen--; /* Make sure there is always space for the nulterm */

    /* The latest history entry is always our current buffer, that
     * initially is just an empty string. */
    linenoiseConfigHistoryAdd(config, "");

    if (write(l.ofd,prompt,l.plen) == -1) return -1;
    while(1) {
        char c;
        int nread;
        char seq[3];

        nread = read(l.ifd,&c,1);
        if (nread <= 0) return l.len;

        /* Only autocomplete when the callback is set. It returns < 0 when
         * there was an error reading from fd. Otherwise it will return the
         * character that should be handled next. */
        if (c == 9 && l.config->completionCallback != NULL) {
            c = completeLine(&l);
            /* Return on errors */
            if (c < 0) return l.len;
            /* Read next character when 0 */
            if (c == 0) continue;
        }

        switch(c) {
        case ENTER:    /* enter */
            l.config->history_len--;
            free(l.config->history[l.config->history_len]);
            if (l.config->mlmode) linenoiseEditMoveEnd(&l);
            if (l.config->hintsCallback) {
                /* Force a refresh without hints to leave the previous
                 * line as the user typed it after a newline. */
                l.hintsdisabled = 1;
                refreshLine(&l);
                l.hintsdisabled = 0;
            }
            return (int)l.len;
        case CTRL_C:     /* ctrl-c */
            errno = EAGAIN;
            return -1;
        case BACKSPACE:   /* backspace */
        case 8:     /* ctrl-h */
            linenoiseEditBackspace(&l);
            break;
        case CTRL_D:     /* ctrl-d, remove char at right of cursor, or if the
                            line is empty, act as end-of-file. */
            if (l.len > 0) {
                linenoiseEditDelete(&l);
            } else {
                l.config->history_len--;
                free(l.config->history[l.config->history_len]);
                return -1;
            }
            break;
        case CTRL_T:    /* ctrl-t, swaps current character with previous. */
            if (l.pos > 0 && l.pos < l.len) {
                int aux = buf[l.pos-1];
                buf[l.pos-1] = buf[l.pos];
                buf[l.pos] = aux;
                if (l.pos != l.len-1) l.pos++;
                refreshLine(&l);
            }
            break;
        case CTRL_B:     /* ctrl-b */
            linenoiseEditMoveLeft(&l);
            break;
        case CTRL_F:     /* ctrl-f */
            linenoiseEditMoveRight(&l);
            break;
        case CTRL_P:    /* ctrl-p */
            linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_PREV);
            break;
        case CTRL_N:    /* ctrl-n */
            linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_NEXT);
            break;
#ifdef __riscos
            /* FIXME: Does not use the escaping sequences yet */
        case CURSOR_UP:
            linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_PREV);
            break;
        case CURSOR_DOWN:
            linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_NEXT);
            break;
        case CURSOR_LEFT:
            linenoiseEditMoveLeft(&l);
            break;
        case CURSOR_RIGHT:
            linenoiseEditMoveRight(&l);
            break;
#else
        case ESC:    /* escape sequence */
            /* Read the next two bytes representing the escape sequence.
             * Use two calls to handle slow terminals returning the two
             * chars at different times. */
            if (read(l.ifd,seq,1) == -1) break;
            if (read(l.ifd,seq+1,1) == -1) break;

            /* ESC [ sequences. */
            if (seq[0] == '[') {
                if (seq[1] >= '0' && seq[1] <= '9') {
                    /* Extended escape, read additional byte. */
                    if (read(l.ifd,seq+2,1) == -1) break;
                    if (seq[2] == '~') {
                        switch(seq[1]) {
                        case '3': /* Delete key. */
                            linenoiseEditDelete(&l);
                            break;
                        }
                    }
                } else {
                    switch(seq[1]) {
                    case 'A': /* Up */
                        linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_PREV);
                        break;
                    case 'B': /* Down */
                        linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_NEXT);
                        break;
                    case 'C': /* Right */
                        linenoiseEditMoveRight(&l);
                        break;
                    case 'D': /* Left */
                        linenoiseEditMoveLeft(&l);
                        break;
                    case 'H': /* Home */
                        linenoiseEditMoveHome(&l);
                        break;
                    case 'F': /* End*/
                        linenoiseEditMoveEnd(&l);
                        break;
                    }
                }
            }

            /* ESC O sequences. */
            else if (seq[0] == 'O') {
                switch(seq[1]) {
                case 'H': /* Home */
                    linenoiseEditMoveHome(&l);
                    break;
                case 'F': /* End*/
                    linenoiseEditMoveEnd(&l);
                    break;
                }
            }
            break;
#endif
        default:
            if (linenoiseEditInsert(&l,c)) return -1;
            break;
        case CTRL_U: /* Ctrl+u, delete the whole line. */
            buf[0] = '\0';
            l.pos = l.len = 0;
            refreshLine(&l);
            break;
        case CTRL_K: /* Ctrl+k, delete from current to end of line. */
            buf[l.pos] = '\0';
            l.len = l.pos;
            refreshLine(&l);
            break;
        case CTRL_A: /* Ctrl+a, go to the start of the line */
            linenoiseEditMoveHome(&l);
            break;
        case CTRL_E: /* ctrl+e, go to the end of the line */
            linenoiseEditMoveEnd(&l);
            break;
        case CTRL_L: /* ctrl+l, clear screen */
            linenoiseClearScreen();
            refreshLine(&l);
            break;
        case CTRL_W: /* ctrl+w, delete previous word */
            linenoiseEditDeletePrevWord(&l);
            break;
        }
    }
#ifdef __riscos
    cursors_restore(&l.cursorstate);
#endif
    return l.len;
}

/* This special mode is used by linenoise in order to print scan codes
 * on screen for debugging / development purposes. It is implemented
 * by the linenoise_example program using the --keycodes option. */
void linenoisePrintKeyCodes(void) {
    char quit[4];

#ifdef __riscos
    cursorstate_t cursorstate;
    cursors_readstate(&cursorstate);
    cursors_keys(&cursorstate, 1);
#endif

    printf("Linenoise key codes debugging mode.\n"
            "Press keys to see scan codes. Type 'quit' at any time to exit.\n");
    if (enableRawMode(STDIN_FILENO) == -1) return;
    memset(quit,' ',4);

    while(1) {
        char c;
        int nread;

        nread = read(STDIN_FILENO,&c,1);
        if (nread <= 0) continue;
        memmove(quit,quit+1,sizeof(quit)-1); /* shift string to left. */
        quit[sizeof(quit)-1] = c; /* Insert current char on the right. */
        if (memcmp(quit,"quit",sizeof(quit)) == 0) break;

        printf("'%c' %02x (%d) (type quit to exit)\n",
            isprint(c) ? c : '?', (int)c, (int)c);
        printf("\r"); /* Go left edge manually, we are in raw mode. */
        fflush(stdout);
    }
#ifdef __riscos
    cursors_restore(&cursorstate);
#endif
    disableRawMode(STDIN_FILENO);
}

/* This function calls the line editing function linenoiseEdit() using
 * the STDIN file descriptor set in raw mode. */
static int linenoiseRaw(struct linenoiseConfig *config, char *buf, size_t buflen, const char *prompt) {
    int count;

    if (buflen == 0) {
        errno = EINVAL;
        return -1;
    }

    if (config == NULL)
        config = &linenoiseGlobalConfig;

    if (enableRawMode(STDIN_FILENO) == -1) return -1;
    count = linenoiseEdit(config, STDIN_FILENO, STDOUT_FILENO, buf, buflen, prompt);
    disableRawMode(STDIN_FILENO);
    printf("\n");
    return count;
}

/* This function is called when linenoise() is called with the standard
 * input file descriptor not attached to a TTY. So for example when the
 * program using linenoise is called in pipe or with a file redirected
 * to its standard input. In this case, we want to be able to return the
 * line regardless of its length (by default we are limited to 4k). */
static char *linenoiseNoTTY(void) {
    char *line = NULL;
    size_t len = 0, maxlen = 0;

    while(1) {
        int c;
        if (len == maxlen) {
            char *oldval;
            if (maxlen == 0) maxlen = 16;
            maxlen *= 2;
            oldval = line;
            line = realloc(line,maxlen);
            if (line == NULL) {
                if (oldval) free(oldval);
                return NULL;
            }
        }
        c = fgetc(stdin);
        if (c == EOF || c == '\n') {
            if (c == EOF && len == 0) {
                free(line);
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

/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
char *linenoise2(struct linenoiseConfig *config, const char *prompt) {
    char buf[LINENOISE_MAX_LINE];
    int count;

    if (config == NULL)
        config = &linenoiseGlobalConfig;

#ifndef __riscos
    if (!isatty(STDIN_FILENO)) {
        /* Not a tty: read from file / pipe. In this mode we don't want any
         * limit to the line size, so we call a function to handle that. */
        return linenoiseNoTTY();
    } else if (isUnsupportedTerm()) {
        size_t len;

        printf("%s",prompt);
        fflush(stdout);
        if (fgets(buf,LINENOISE_MAX_LINE,stdin) == NULL) return NULL;
        len = strlen(buf);
        while(len && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            len--;
            buf[len] = '\0';
        }
        return strdup(buf);
    } else
#else
    /* It doesn't make sense to not have a TTY on RISC OS */
#endif
    {
        count = linenoiseRaw(config, buf,LINENOISE_MAX_LINE,prompt);
        if (count == -1) return NULL;
        return strdup(buf);
    }
}

/* This is just a wrapper the user may want to call in order to make sure
 * the linenoise returned buffer is freed with the same allocator it was
 * created with. Useful when the main program is using an alternative
 * allocator. */
void linenoiseFree(void *ptr) {
    free(ptr);
}

/* ================================ History ================================= */

/* Free the history, but does not reset it. Only used when we have to
 * exit() to avoid memory leaks are reported by valgrind & co. */
static void freeHistory(struct linenoiseConfig *config) {
    if (config->history) {
        int j;

        for (j = 0; j < config->history_len; j++)
            free(config->history[j]);
        free(config->history);
    }
}

#ifndef __riscos
/* At exit we'll try to fix the terminal to the initial conditions. */
static void linenoiseAtExit(void) {
    disableRawMode(STDIN_FILENO);
    freeHistory(&linenoiseGlobalConfig);
}
#endif

/* This is the API call to add a new entry in the linenoise history.
 * It uses a fixed array of char pointers that are shifted (memmoved)
 * when the history max length is reached in order to remove the older
 * entry and make room for the new one, so it is not exactly suitable for huge
 * histories, but will work well for a few hundred of entries.
 *
 * Using a circular buffer is smarter, but a bit more complex to handle. */
int linenoiseConfigHistoryAdd(struct linenoiseConfig *config, const char *line) {
    char *linecopy;

    if (config == NULL)
        config = &linenoiseGlobalConfig;

    if (config->history_max_len == 0) return 0;

    /* Initialization on first call. */
    if (config->history == NULL) {
        config->history = malloc(sizeof(char*) * config->history_max_len);
        if (config->history == NULL) return 0;
        memset(config->history,0,(sizeof(char*) * config->history_max_len));
    }

    /* Don't add duplicated lines. */
    if (config->history_len && !strcmp(config->history[config->history_len-1], line)) return 0;

    /* Add an heap allocated copy of the line in the history.
     * If we reached the max length, remove the older line. */
    linecopy = strdup(line);
    if (!linecopy) return 0;
    if (config->history_len == config->history_max_len) {
        free(config->history[0]);
        memmove(config->history, config->history+1,sizeof(char*)*(config->history_max_len-1));
        config->history_len--;
    }
    config->history[config->history_len] = linecopy;
    config->history_len++;
    return 1;
}

/* Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. */
int linenoiseConfigHistorySetMaxLen(struct linenoiseConfig *config, int len) {
    char **new;

    if (config == NULL)
        config = &linenoiseGlobalConfig;

    if (len < 1) return 0;
    if (config->history) {
        int tocopy = config->history_len;

        new = malloc(sizeof(char*)*len);
        if (new == NULL) return 0;

        /* If we can't copy everything, free the elements we'll not use. */
        if (len < tocopy) {
            int j;

            for (j = 0; j < tocopy-len; j++) free(config->history[j]);
            tocopy = len;
        }
        memset(new,0,sizeof(char*)*len);
        memcpy(new, config->history+(config->history_len-tocopy), sizeof(char*)*tocopy);
        free(config->history);
        config->history = new;
    }
    config->history_max_len = len;
    if (config->history_len > config->history_max_len)
        config->history_len = config->history_max_len;
    return 1;
}

/* Save the history in the specified file. On success 0 is returned
 * otherwise -1 is returned. */
int linenoiseConfigHistorySave(struct linenoiseConfig *config, const char *filename) {
#ifndef __riscos
    mode_t old_umask = umask(S_IXUSR|S_IRWXG|S_IRWXO);
#endif
    FILE *fp;
    int j;

    if (config == NULL)
        config = &linenoiseGlobalConfig;

    fp = fopen(filename,"w");
#ifndef __riscos
    umask(old_umask);
#endif
    if (fp == NULL) return -1;
#ifndef __riscos
    chmod(filename,S_IRUSR|S_IWUSR);
#endif
    for (j = 0; j < config->history_len; j++)
        fprintf(fp,"%s\n", config->history[j]);
    fclose(fp);
    return 0;
}

/* Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int linenoiseConfigHistoryLoad(struct linenoiseConfig *config, const char *filename) {
    FILE *fp = fopen(filename,"r");
    char buf[LINENOISE_MAX_LINE];

    if (fp == NULL) return -1;

    while (fgets(buf,LINENOISE_MAX_LINE,fp) != NULL) {
        char *p;

        p = strchr(buf,'\r');
        if (!p) p = strchr(buf,'\n');
        if (p) *p = '\0';
        linenoiseConfigHistoryAdd(config, buf);
    }
    fclose(fp);
    return 0;
}

/* ======================= Compatibility ====================== */

char *linenoise(const char *prompt)
{
    return linenoise2(NULL, prompt);
}

int linenoiseHistoryAdd(const char *line)
{
    return linenoiseConfigHistoryAdd(NULL, line);
}

int linenoiseHistorySetMaxLen(int len)
{
    return linenoiseConfigHistorySetMaxLen(NULL, len);
}

int linenoiseHistorySave(const char *filename)
{
    return linenoiseConfigHistorySave(NULL, filename);
}

int linenoiseHistoryLoad(const char *filename)
{
    return linenoiseConfigHistoryLoad(NULL, filename);
}

void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn) {
    linenoiseConfigSetCompletionCallback(NULL, fn);
}

/* Register a hits function to be called to show hits to the user at the
 * right of the prompt. */
void linenoiseSetHintsCallback(linenoiseHintsCallback *fn) {
    linenoiseConfigSetHintsCallback(NULL, fn);
}

/* Register a function to free the hints returned by the hints callback
 * registered with linenoiseSetHintsCallback(). */
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *fn) {
    linenoiseConfigSetFreeHintsCallback(NULL, fn);
}
