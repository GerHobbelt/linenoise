/*
 * Copyright (c) 2010-2016, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2020, Shadow Yuan <shadow-yuan@qq.com>
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
 * You can find the latest source code at:
 *
 *   https://github.com/shadow-yuan/linenoise-win32
 *
 * Aims: Linenoise Win32 Version.
 *
 * Modify: 8/17 2020
 *
 */

#include <assert.h>
#include <io.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <windows.h>

#include "linenoise.h"
#include "ht.h"

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1

//static char *unsupported_term[] = { "dumb","cons25","emacs",NULL };
static linenoiseCompletionCallback *completionCallback = NULL;
static linenoiseHintsCallback *hintsCallback = NULL;
static linenoiseFreeHintsCallback *freeHintsCallback = NULL;

static int maskmode = 0; /* Show "***" instead of input. For passwords. */
static int rawmode = 0; /* For atexit() function to check if restore is needed*/
static int mlmode = 0;  /* Multi line mode. Default is single line. */
static int atexit_registered = 0; /* Register atexit just 1 time. */
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char **history = NULL;
static struct HashTable ht;

int LINENOISE_FOREGROUND_BLUE = FOREGROUND_BLUE;
int LINENOISE_FOREGROUND_GREEN = FOREGROUND_GREEN;
int LINENOISE_FOREGROUND_RED = FOREGROUND_RED;
int LINENOISE_FOREGROUND_INTENSITY = FOREGROUND_INTENSITY;
int LINENOISE_BACKGROUND_BLUE = BACKGROUND_BLUE;
int LINENOISE_BACKGROUND_GREEN = BACKGROUND_GREEN;
int LINENOISE_BACKGROUND_RED = BACKGROUND_RED;
int LINENOISE_BACKGROUND_INTENSITY = BACKGROUND_INTENSITY;

/* The linenoiseState structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct linenoiseState {
    HANDLE ifd;            /* Terminal stdin file descriptor. */
    HANDLE ofd;            /* Terminal stdout file descriptor. */
    char *buf;          /* Edited line buffer. */
    size_t buflen;      /* Edited line buffer size. */
    const char *prompt; /* Prompt to display. */
    size_t plen;        /* Prompt length. */
    size_t pos;         /* Current cursor position. */
    size_t oldpos;      /* Previous refresh cursor position. */
    size_t len;         /* Current edited line length. */
    size_t cols;        /* Number of columns in terminal. */
    size_t crow;        // current cursor row
    size_t maxrows;     /* Maximum num of rows used so far (multiline mode) */
    int history_index;  /* The history index we are currently editing. */
};

static void linenoiseAtExit(void);
int linenoiseHistoryAdd(const char *line);
static void refreshLine(struct linenoiseState *l);

#define KEYEVENT_LOOP_EXIT -1
#define KEYEVENT_LOOP_CONTINUE 0
#define KEYEVENT_LOOP_EXIT_WITH_LENGTH 1

typedef int(*KeyEventCallback)(struct linenoiseState* l, KEY_EVENT_RECORD* ker);
int DefaultKeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker);

/* Debugging macro. */
#if 0
FILE *lndebug_fp = NULL;
#define lndebug(...) \
    do { \
        if (lndebug_fp == NULL) { \
            lndebug_fp = fopen("lndebug.txt","a"); \
            fprintf(lndebug_fp, \
            "[%d %d %d] p: %d, rows: %d, rpos: %d, max: %d, oldmax: %d\n", \
            (int)l->len,(int)l->pos,(int)l->oldpos,plen,rows,rpos, \
            (int)l->maxrows,old_rows); \
        } \
        fprintf(lndebug_fp, ", " __VA_ARGS__); \
        fflush(lndebug_fp); \
    } while (0)
#else
#define lndebug(fmt, ...)
#endif

static int  RedisWin32Write(HANDLE handle, const char* buf, size_t bytes) {
    DWORD WriteBytes = 0;
    if (!WriteFile(handle, buf, bytes, &WriteBytes, NULL) && WriteBytes == 0) {
        return -1;
    }
    return (int)WriteBytes;
}

static int RedisWin32Read(HANDLE handle, char *__buf, size_t __nbytes) {
    //intptr_t handle = _get_osfhandle(fd);
    DWORD ReadBytes = 0;
    if (!ReadFile(handle, __buf, __nbytes, &ReadBytes, NULL) && ReadBytes == 0) {
        return -1;
    }
    return (int)ReadBytes;
}

static uint32_t RegisterKeyEvent(uint8_t ctrl, uint8_t alt, uint8_t shift, uint8_t key) {
    unsigned char buf[4] = { ctrl, alt, shift, key };
    return GetUInt32ByLittleEndian(buf);
}

static char* FormatErrorMessage(int messageid) {
    char* error_text = NULL;
    // Use MBCS version of FormatMessage to match return value.
    DWORD status = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, (DWORD)messageid, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT),
        (char*)(&error_text), 0, NULL);
    if (status == 0) return _strdup("Unable to retrieve error string");
    char* message = _strdup(error_text);
    LocalFree(error_text);
    return message;
}

/* ======================= Low level terminal handling ====================== */

/* Enable "mask mode". When it is enabled, instead of the input that
 * the user is typing, the terminal will just display a corresponding
 * number of asterisks, like "****". This is useful for passwords and other
 * secrets that should not be displayed. */
void linenoiseMaskModeEnable(void) {
    maskmode = 1;
}

/* Disable mask mode. */
void linenoiseMaskModeDisable(void) {
    maskmode = 0;
}

/* Set if to use or not the multi line mode. */
void linenoiseSetMultiLine(int ml) {
    mlmode = ml;
}

/* Return true if the terminal name is in the list of terminals we know are
 * not able to understand basic escape sequences. */
static int isUnsupportedTerm(void) {
    return 0;
    /*
    char *term = getenv("TERM");
    int j;

    if (term == NULL) return 0;
    for (j = 0; unsupported_term[j]; j++)
        if (!_stricmp(term,unsupported_term[j])) return 1;
    return 0;
    */
}

/* Raw mode: 1960 magic shit. */

static int enableRawMode() {
    if (!_isatty(_fileno(stdin))) goto fatal;
    if (!atexit_registered) {
        atexit(linenoiseAtExit);
        atexit_registered = 1;
    }

    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
    rawmode = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

static void disableRawMode() {
    /* Don't even check the return value as it's too late. */
    if (rawmode) {
        FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
        rawmode = 0;
    }
}

static int setCursorPosition(HANDLE stdout_handle, int row, int col) {
    COORD pos;
    pos.X = col;
    pos.Y = row;
    SetConsoleCursorPosition(stdout_handle, pos);
    return 0;
}

/* Try to get the number of columns in the current terminal, or assume 80
 * if it fails. */

static void getWindowSize(HANDLE ofd, int* cursor_row, int* col_width) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(ofd, &info)) {
        *cursor_row = 0;
        *col_width = 80;
        return;
    }

    *cursor_row = info.dwCursorPosition.Y;
    *col_width = info.dwSize.X;
}

static int receiveOneKeyEvent(INPUT_RECORD* pir, int* cols_change) {
    DWORD fdwSaveOldMode = 0, fdwMode = 0;
    DWORD Count = 0;

    BOOL ret;
    HANDLE hFile;

    while (1) {

        Count = 0;
        fdwMode = ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
        hFile = GetStdHandle(STD_INPUT_HANDLE);
        GetConsoleMode(hFile, &fdwSaveOldMode);

        SetConsoleMode(hFile, fdwMode);
        ret = ReadConsoleInput(hFile, pir, 1, &Count);
        SetConsoleMode(hFile, fdwSaveOldMode);

        if (!ret || Count == 0) {
            return -1;// (int)l.len;
        }

        if (pir->EventType != KEY_EVENT) {
            if (pir->EventType == WINDOW_BUFFER_SIZE_EVENT) {
                *cols_change = pir->Event.WindowBufferSizeEvent.dwSize.X;
            }
            continue;
        }

        if (!pir->Event.KeyEvent.bKeyDown) {
            continue;
        }

        break;
    };

    return 0;
}


/* Clear the screen. Used to handle ctrl+l */
void linenoiseClearScreen(void) {
    COORD coordScreen = { 0, 0 };
    DWORD cCharsWritten = 0, dwConSize;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(hConsole, &csbi);

    dwConSize = csbi.dwSize.X * csbi.dwSize.Y;
    FillConsoleOutputCharacter(hConsole, TEXT(' '), dwConSize, coordScreen, &cCharsWritten);
    FillConsoleOutputAttribute(hConsole, csbi.wAttributes, dwConSize, coordScreen, &cCharsWritten);
    SetConsoleCursorPosition(hConsole, coordScreen);
}

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
static void linenoiseBeep(void) {
    Beep(450, 400);
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
    WORD c = 0;
    int ret = 0;
    INPUT_RECORD ir;

    completionCallback(ls->buf, &lc);
    if (lc.len == 0) {
        linenoiseBeep();
    } else {
        size_t stop = 0, i = 0;

        while (!stop) {
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

            nread = receiveOneKeyEvent(&ir, &ls->cols);
            if (nread < 0) {
                freeCompletions(&lc);
                return -1;
            }

            c = ir.Event.KeyEvent.wVirtualKeyCode;
            ret = ir.Event.KeyEvent.uChar.AsciiChar;

            switch (c) {
            case VK_TAB:
                i = (i + 1) % (lc.len + 1);
                if (i == lc.len) linenoiseBeep();
                break;
            case VK_ESCAPE:
                /* Re-show original buffer */
                if (i < lc.len) refreshLine(ls);
                stop = 1;
                break;
            default:
                /* Update buffer and return */
                if (i < lc.len) {
                    nwritten = snprintf(ls->buf, ls->buflen, "%s", lc.cvec[i]);
                    ls->len = ls->pos = nwritten;
                }
                stop = 1;
                break;
            }
        }
    }

    freeCompletions(&lc);
    return ret; /* Return last read character */
}

/* Register a callback function to be called for tab-completion. */
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn) {
    completionCallback = fn;
}

/* Register a hits function to be called to show hits to the user at the
 * right of the prompt. */
void linenoiseSetHintsCallback(linenoiseHintsCallback *fn) {
    hintsCallback = fn;
}

/* Register a function to free the hints returned by the hints callback
 * registered with linenoiseSetHintsCallback(). */
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *fn) {
    freeHintsCallback = fn;
}

/* This function is used by the callback function registered by the user
 * in order to add completion options given the input string when the
 * user typed <tab>. See the example.c source code for a very easy to
 * understand example. */
void linenoiseAddCompletion(linenoiseCompletions *lc, const char *str) {
    size_t len = strlen(str);
    char *copy, **cvec;

    copy = malloc(len + 1);
    if (copy == NULL) return;
    memcpy(copy, str, len + 1);
    cvec = realloc(lc->cvec, sizeof(char*)*(lc->len + 1));
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
 * RedisWin32Write all the escape sequences in a buffer and flush them to the standard
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
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(new + ab->len, s, len);
    ab->b = new;
    ab->len += len;
}

static void abFree(struct abuf *ab) {
    free(ab->b);
}

static void updateScreenSingleLine(HANDLE hFile, int cur_row, int width, struct abuf* prefix, struct abuf* hits, int color) {
    CHAR_INFO* cout_info;
    SMALL_RECT rect;
    COORD sz, pos;
    int index;

    cout_info = (CHAR_INFO*) malloc(sizeof(CHAR_INFO) * width);

    index = 0;

    if (prefix) {
        for (int i = 0; i < prefix->len; i++) {
            cout_info[index].Attributes = 7;
            cout_info[index].Char.UnicodeChar = prefix->b[i];
            index++;
        }
    }

    if (hits) {
        for (int j = 0; j < hits->len; j++) {
            cout_info[index].Attributes = 7 | color;
            cout_info[index].Char.UnicodeChar = hits->b[j];
            index++;
        }
    }

    for (int k = index; k < width; k++) {
        cout_info[k].Attributes = 7;
        cout_info[k].Char.UnicodeChar = TEXT(' ');
    }

    sz.X = width; sz.Y = 1; // X * Y = count of CHAR_INFO
    pos.X = pos.Y = 0;

    rect.Top = cur_row;
    rect.Left = 0;
    rect.Right = width - 1;
    rect.Bottom = cur_row;

    WriteConsoleOutput(hFile, cout_info, sz, pos, &rect);

    free(cout_info);
}


/* Helper of refreshSingleLine() and refreshMultiLine() to show hints
 * to the right of the prompt. */
int refreshShowHints(struct abuf *ab, struct linenoiseState *l, int plen, struct abuf* output_hints) {
    if (hintsCallback && plen + l->len < l->cols) {
        int color = 0, bold = 0;
        char *hint = hintsCallback(l->buf, &color, &bold);
        if (hint) {
            int hintlen = strlen(hint);
            int hintmaxlen = l->cols - (plen + l->len);
            if (hintlen > hintmaxlen) hintlen = hintmaxlen;

            output_hints->b = _strdup(hint);
            output_hints->len = strlen(output_hints->b);

            /* Call the function to free the hint returned. */
            if (freeHintsCallback) freeHintsCallback(hint);

            return color;
        }
    }
    return 0;
}

/* Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void refreshSingleLine(struct linenoiseState *l) {
    size_t plen = strlen(l->prompt);

    char *buf = l->buf;
    size_t len = l->len;
    size_t pos = l->pos;
    struct abuf ab;
    struct abuf hits;
    int color = 0;

    while ((plen + pos) >= l->cols) {
        buf++;
        len--;
        pos--;
    }
    while (plen + len > l->cols) {
        len--;
    }

    abInit(&ab);
    abInit(&hits);

    /* Write the prompt and the current buffer content */
    abAppend(&ab, l->prompt, strlen(l->prompt));
    if (maskmode == 1) {
        while (len--) abAppend(&ab, "*", 1);
    } else {
        abAppend(&ab, buf, len);
    }
    /* Show hits if any. */
    color = refreshShowHints(&ab, l, plen, &hits);

    updateScreenSingleLine(l->ofd, l->crow, l->cols, &ab, &hits, color);

    /* Move cursor to original position. */
    setCursorPosition(l->ofd, l->crow, pos + plen);

    abFree(&ab);
    abFree(&hits);
}

/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void refreshMultiLine(struct linenoiseState *l) {
    int plen = strlen(l->prompt);
    int rows = (plen + l->len + l->cols - 1) / l->cols; /* rows used by current buf. */

    struct abuf ab;
    struct abuf hits;
    int color;
    int off;

    /* Update maxrows if needed. */
    if (rows > (int)l->maxrows) l->maxrows = rows;

    /* First step: clear all the lines used before. To do so start by
     * going to the last row. */
    abInit(&ab);
    abInit(&hits);

    /* Write the prompt and the current buffer content */
    abAppend(&ab, l->prompt, strlen(l->prompt));
    if (maskmode == 1) {
        unsigned int i;
        for (i = 0; i < l->len; i++) abAppend(&ab, "*", 1);
    } else {
        abAppend(&ab, l->buf, l->len);
    }

    /* Show hits if any. */
    color = refreshShowHints(&ab, l, plen, &hits);

    if (ab.len + hits.len <= (int)l->cols) {
        updateScreenSingleLine(l->ofd, l->crow, l->cols, &ab, &hits, color);
    } else {
        int row_index = 0;
        int r_bytes = ab.len;
        char* temp = ab.b;
        struct abuf prefix;
        struct abuf suffix;
        while (r_bytes >= (int)l->cols) {
            prefix.b = temp;
            prefix.len = l->cols;
            updateScreenSingleLine(l->ofd, l->crow + row_index, l->cols, &prefix, NULL, 0);
            temp += l->cols;
            r_bytes -= l->cols;
            row_index++;
        }

        if (r_bytes > 0) {
            prefix.b = temp;
            prefix.len = r_bytes;
            suffix.b = hits.b;
            suffix.len = min((int)l->cols - r_bytes, hits.len);
            updateScreenSingleLine(l->ofd, l->crow + row_index , l->cols, &prefix, &suffix, color);
            temp = hits.b + suffix.len;
            r_bytes = hits.len - suffix.len;
            row_index++;
        }

        while (r_bytes >= (int)l->cols) {
            suffix.b = temp;
            suffix.len = l->cols;
            updateScreenSingleLine(l->ofd, l->crow + row_index, l->cols, NULL, &suffix, color);
            temp += l->cols;
            r_bytes -= l->cols;
            row_index++;
        }
        if (r_bytes > 0) {
            suffix.b = temp;
            suffix.len = r_bytes;
            updateScreenSingleLine(l->ofd, l->crow + row_index, l->cols, NULL, &suffix, color);
            row_index++;
        }
    }

    /* Move cursor to right position. */

    if (l->pos + l->plen < l->cols) {
        setCursorPosition(l->ofd, l->crow, l->pos + l->plen);
    } else {
        rows = (l->pos + l->plen) / l->cols;
        off = (l->pos + l->plen) % l->cols;
        setCursorPosition(l->ofd, l->crow + rows, off);
    }

    l->oldpos = l->pos;

    abFree(&hits);
    abFree(&ab);
}

/* Calls the two low level functions refreshSingleLine() or
 * refreshMultiLine() according to the selected mode. */
static void refreshLine(struct linenoiseState *l) {
    if (mlmode)
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
            if ((!mlmode && l->plen + l->len < l->cols && !hintsCallback)) {
                /* Avoid a full update of the line in the
                 * trivial case. */
                char d = (maskmode == 1) ? '*' : c;
                if (RedisWin32Write(l->ofd, &d, 1) == -1) return -1;
            } else {
                refreshLine(l);
            }
        } else {
            memmove(l->buf + l->pos + 1, l->buf + l->pos, l->len - l->pos);
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
void linenoiseEditHistoryNext(struct linenoiseState *l, int dir) {
    if (history_len > 1) {
        /* Update the current history entry before to
         * overwrite it with the next one. */
        free(history[history_len - 1 - l->history_index]);
        history[history_len - 1 - l->history_index] = _strdup(l->buf);
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
        refreshLine(l);
    }
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
void linenoiseEditDelete(struct linenoiseState *l) {
    if (l->len > 0 && l->pos < l->len) {
        memmove(l->buf + l->pos, l->buf + l->pos + 1, l->len - l->pos - 1);
        l->len--;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

/* Backspace implementation. */
void linenoiseEditBackspace(struct linenoiseState *l) {
    if (l->pos > 0 && l->len > 0) {
        memmove(l->buf + l->pos - 1, l->buf + l->pos, l->len - l->pos);
        l->pos--;
        l->len--;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

/* Delete the previous word, maintaining the cursor at the start of the
 * current word. */
void linenoiseEditDeletePrevWord(struct linenoiseState *l) {
    size_t old_pos = l->pos;
    size_t diff;

    while (l->pos > 0 && l->buf[l->pos - 1] == ' ')
        l->pos--;
    while (l->pos > 0 && l->buf[l->pos - 1] != ' ')
        l->pos--;
    diff = old_pos - l->pos;
    memmove(l->buf + l->pos, l->buf + old_pos, l->len - old_pos + 1);
    l->len -= diff;
    refreshLine(l);
}

/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to RedisWin32Read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
static int linenoiseEdit(HANDLE stdin_fd, HANDLE stdout_fd, char *buf, size_t buflen, const char *prompt) {
    struct linenoiseState l;

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
    l.cols = 0;
    l.crow = 0;
    l.maxrows = 0;
    l.history_index = 0;

    getWindowSize(l.ofd, &l.crow, &l.cols);

    /* Buffer starts empty. */
    l.buf[0] = '\0';
    l.buflen--; /* Make sure there is always space for the nulterm */

    /* The latest history entry is always our current buffer, that
     * initially is just an empty string. */
    linenoiseHistoryAdd("");

    if (RedisWin32Write(l.ofd, prompt, l.plen) == -1) return -1;

    while (1) {

        int ctrl = 0, alt = 0, shift = 0;
        int cbret = 0;
        KeyEventCallback callback = NULL;
        INPUT_RECORD ir;
        uint32_t hashkey;

        cbret = receiveOneKeyEvent(&ir, &l.cols);
        if (cbret < 0) {
            return (int)l.len;
        }

        if (ir.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED) {
            ctrl = 1;
        }
        if (ir.Event.KeyEvent.dwControlKeyState & RIGHT_CTRL_PRESSED) {
            ctrl = 2;
        }
        if (ir.Event.KeyEvent.dwControlKeyState & LEFT_ALT_PRESSED) {
            alt = 1;
        }
        if (ir.Event.KeyEvent.dwControlKeyState & RIGHT_ALT_PRESSED) {
            alt = 2;
        }
        if (ir.Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED) {
            shift = 1;
        }

        hashkey = RegisterKeyEvent(ctrl, alt, shift, (uint8_t)ir.Event.KeyEvent.wVirtualKeyCode);
        callback = (KeyEventCallback)HashTableLookup(&ht, hashkey);
        if (!callback) {
            cbret = DefaultKeyEvent(&l, &ir.Event.KeyEvent);
        } else {
            cbret = callback(&l, &ir.Event.KeyEvent);
        }

        if (cbret == KEYEVENT_LOOP_EXIT) {
            return cbret;
        }
        if (cbret == KEYEVENT_LOOP_EXIT_WITH_LENGTH) {
            return (int)l.len;
        }
    }
    return l.len;
}

/* This special mode is used by linenoise in order to print scan codes
 * on screen for debugging / development purposes. It is implemented
 * by the linenoise_example program using the --keycodes option. */
void linenoisePrintKeyCodes(void) {
    char quit[4];

    printf("Linenoise key codes debugging mode.\n"
        "Press keys to see scan codes. Type 'quit' at any time to exit.\n");
    if (enableRawMode(_fileno(stdin)) == -1) return;
    memset(quit, ' ', 4);
    while (1) {
        char c;
        int nread;

        nread = RedisWin32Read(GetStdHandle(STD_INPUT_HANDLE), &c, 1);
        if (nread <= 0) continue;
        memmove(quit, quit + 1, sizeof(quit) - 1); /* shift string to left. */
        quit[sizeof(quit) - 1] = c; /* Insert current char on the right. */
        if (memcmp(quit, "quit", sizeof(quit)) == 0) break;

        printf("'%c' %02x (%d) (type quit to exit)\n",
            isprint(c) ? c : '?', (int)c, (int)c);
        printf("\r"); /* Go left edge manually, we are in raw mode. */
        fflush(stdout);
    }
    disableRawMode(_fileno(stdin));
}

/* This function calls the line editing function linenoiseEdit() using
 * the STDIN file descriptor set in raw mode. */
static int linenoiseRaw(char *buf, size_t buflen, const char *prompt) {
    HANDLE input, output;
    int count;

    if (buflen == 0) {
        errno = EINVAL;
        return -1;
    }

    if (enableRawMode() == -1) return -1;

    input = GetStdHandle(STD_INPUT_HANDLE);
    output = GetStdHandle(STD_OUTPUT_HANDLE);

    count = linenoiseEdit(input, output, buf, buflen, prompt);

    disableRawMode();
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

    while (1) {
        if (len == maxlen) {
            if (maxlen == 0) maxlen = 16;
            maxlen *= 2;
            char *oldval = line;
            line = realloc(line, maxlen);
            if (line == NULL) {
                if (oldval) free(oldval);
                return NULL;
            }
        }
        int c = fgetc(stdin);
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
char *linenoise(const char *prompt) {
    char buf[LINENOISE_MAX_LINE];
    int count;

    if (!_isatty(_fileno(stdin))) { // _fileno(stdin)
        /* Not a tty: RedisWin32Read from file / pipe. In this mode we don't want any
         * limit to the line size, so we call a function to handle that. */
        return linenoiseNoTTY();
    } else if (isUnsupportedTerm()) {
        size_t len;

        printf("%s", prompt);
        fflush(stdout);
        if (fgets(buf, LINENOISE_MAX_LINE, stdin) == NULL) return NULL;
        len = strlen(buf);
        while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
            len--;
            buf[len] = '\0';
        }
        return _strdup(buf);
    } else {
        count = linenoiseRaw(buf, LINENOISE_MAX_LINE, prompt);
        if (count == -1) return NULL;
        return _strdup(buf);
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
    disableRawMode(_fileno(stdin)); // _fileno(stdin)
    freeHistory();
}

/* This is the API call to add a new entry in the linenoise history.
 * It uses a fixed array of char pointers that are shifted (memmoved)
 * when the history max length is reached in order to remove the older
 * entry and make room for the new one, so it is not exactly suitable for huge
 * histories, but will work well for a few hundred of entries.
 *
 * Using a circular buffer is smarter, but a bit more complex to handle. */
int linenoiseHistoryAdd(const char *line) {
    char *linecopy;

    if (history_max_len == 0) return 0;

    /* Initialization on first call. */
    if (history == NULL) {
        history = malloc(sizeof(char*)*history_max_len);
        if (history == NULL) return 0;
        memset(history, 0, (sizeof(char*)*history_max_len));
    }

    /* Don't add duplicated lines. */
    if (history_len && !strcmp(history[history_len - 1], line)) return 0;

    /* Add an heap allocated copy of the line in the history.
     * If we reached the max length, remove the older line. */
    linecopy = _strdup(line);
    if (!linecopy) return 0;
    if (history_len == history_max_len) {
        free(history[0]);
        memmove(history, history + 1, sizeof(char*)*(history_max_len - 1));
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

            for (j = 0; j < tocopy - len; j++) free(history[j]);
            tocopy = len;
        }
        memset(new, 0, sizeof(char*)*len);
        memcpy(new, history + (history_len - tocopy), sizeof(char*)*tocopy);
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
int linenoiseHistorySave(const char *filename) {
    FILE *fp;
    int j;

    fp = fopen(filename, "w");
    if (fp == NULL) return -1;

    for (j = 0; j < history_len; j++)
        fprintf(fp, "%s\n", history[j]);
    fclose(fp);
    return 0;
}

/* Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int linenoiseHistoryLoad(const char *filename) {
    FILE *fp = fopen(filename, "r");
    char buf[LINENOISE_MAX_LINE];

    if (fp == NULL) return -1;

    while (fgets(buf, LINENOISE_MAX_LINE, fp) != NULL) {
        char *p;

        p = strchr(buf, '\r');
        if (!p) p = strchr(buf, '\n');
        if (p) *p = '\0';
        linenoiseHistoryAdd(buf);
    }
    fclose(fp);
    return 0;
}

// ------------------------------------------------------------------------
//
// key pressed event
//
int Tab_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* Only autocomplete when the callback is set. It returns < 0 when
     * there was an error reading from fd. Otherwise it will return the
     * character that should be handled next. */
    WORD c = ker->wVirtualKeyCode;
    assert(c == VK_TAB);

    if (completionCallback != NULL) {
        c = completeLine(l);
        /* Return on errors */
        if (c < 0) return KEYEVENT_LOOP_EXIT_WITH_LENGTH;
        /* Read next character when 0 */
        if (c == 0) return KEYEVENT_LOOP_CONTINUE;
    }
    return KEYEVENT_LOOP_CONTINUE;
}

int Enter_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* enter */
    WORD c = ker->wVirtualKeyCode;
    assert(c == VK_RETURN);

    history_len--;
    free(history[history_len]);
    if (mlmode) linenoiseEditMoveEnd(l);
    if (hintsCallback) {
        /* Force a refresh without hints to leave the previous
            * line as the user typed it after a newline. */
        linenoiseHintsCallback *hc = hintsCallback;
        hintsCallback = NULL;
        refreshLine(l);
        hintsCallback = hc;
    }
    return KEYEVENT_LOOP_EXIT_WITH_LENGTH;
}

int CTRL_C_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* ctrl-c */
    WORD c = ker->wVirtualKeyCode;
    int ctrl = 0;

    if (ker->dwControlKeyState & LEFT_CTRL_PRESSED) {
        ctrl = 1;
    }
    if (ker->dwControlKeyState & RIGHT_CTRL_PRESSED) {
        ctrl = 2;
    }

    assert(c == 'C');
    assert(ctrl != 0);
    errno = EAGAIN;
    return KEYEVENT_LOOP_EXIT;
}

int Backspace_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* backspace */
    /* ctrl-h */
    WORD c = ker->wVirtualKeyCode;
    int ctrl = 0;

    if (ker->dwControlKeyState & LEFT_CTRL_PRESSED) {
        ctrl = 1;
    }
    if (ker->dwControlKeyState & RIGHT_CTRL_PRESSED) {
        ctrl = 2;
    }

    assert(c == VK_BACK || c == 'H' && ctrl != 0);
    linenoiseEditBackspace(l);
    return KEYEVENT_LOOP_CONTINUE;
}

int CTRL_D_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* ctrl-d, remove char at right of cursor, or if the
       line is empty, act as end-of-file. */
    if (l->len > 0) {
        linenoiseEditDelete(l);
        return KEYEVENT_LOOP_CONTINUE;
    } else {
        history_len--;
        free(history[history_len]);
        return KEYEVENT_LOOP_EXIT;
    }
}

int CTRL_T_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* ctrl-t, swaps current character with previous. */
    if (l->pos > 0 && l->pos < l->len) {
        int aux = l->buf[l->pos - 1];
        l->buf[l->pos - 1] = l->buf[l->pos];
        l->buf[l->pos] = aux;
        if (l->pos != l->len - 1) l->pos++;
        refreshLine(l);
    }
    return KEYEVENT_LOOP_CONTINUE;
}

int CTRL_B_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* ctrl-b */
    linenoiseEditMoveLeft(l);
    return KEYEVENT_LOOP_CONTINUE;
}

int CTRL_F_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* ctrl-f */
    linenoiseEditMoveRight(l);
    return KEYEVENT_LOOP_CONTINUE;
}

int CTRL_P_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* ctrl-p */
    linenoiseEditHistoryNext(l, LINENOISE_HISTORY_PREV);
    return KEYEVENT_LOOP_CONTINUE;
}

int CTRL_N_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* ctrl-n */
    linenoiseEditHistoryNext(l, LINENOISE_HISTORY_NEXT);
    return KEYEVENT_LOOP_CONTINUE;
}

int Escape_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* Read the next two bytes representing the escape sequence.
     * Use two calls to handle slow terminals returning the two
     * chars at different times. */
    WORD c = ker->wVirtualKeyCode;
    assert(c == VK_ESCAPE);
    return KEYEVENT_LOOP_CONTINUE;
}

int CTRL_U_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* Ctrl+u, delete the whole line. */
    l->buf[0] = '\0';
    l->pos = l->len = 0;
    refreshLine(l);
    return KEYEVENT_LOOP_CONTINUE;
}

int CTRL_K_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* Ctrl+k, delete from current to end of line. */
    l->buf[l->pos] = '\0';
    l->len = l->pos;
    refreshLine(l);
    return KEYEVENT_LOOP_CONTINUE;
}

int CTRL_A_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* Ctrl+a, go to the start of the line */
    linenoiseEditMoveHome(l);
    return KEYEVENT_LOOP_CONTINUE;
}

int CTRL_E_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* ctrl+e, go to the end of the line */
    linenoiseEditMoveEnd(l);
    return KEYEVENT_LOOP_CONTINUE;
}

int CTRL_L_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* ctrl+l, clear screen */
    linenoiseClearScreen();
    refreshLine(l);
    return KEYEVENT_LOOP_CONTINUE;
}

int CTRL_W_KeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    /* ctrl+w, delete previous word */
    linenoiseEditDeletePrevWord(l);
    return KEYEVENT_LOOP_CONTINUE;
}

int DefaultKeyEvent(struct linenoiseState* l, KEY_EVENT_RECORD* ker) {
    char c = ker->uChar.AsciiChar;
    if (isprint(c)) {
        if (linenoiseEditInsert(l, c)) {
            return KEYEVENT_LOOP_EXIT;
        }
    }
    return KEYEVENT_LOOP_CONTINUE;
}

void linenoiseInit() {
    uint32_t key = 0;

    HashTableInit(&ht);

    // ctrl | alt | shift | key
    key = RegisterKeyEvent(0, 0, 0, VK_TAB);  // TAB
    HashTableInsert(&ht, key, Tab_KeyEvent);

    key = RegisterKeyEvent(0, 0, 0, VK_RETURN); // ENTER
    HashTableInsert(&ht, key, Enter_KeyEvent);

    key = RegisterKeyEvent(1, 0, 0, 'C'); // CTRL+C
    HashTableInsert(&ht, key, CTRL_C_KeyEvent);

    key = RegisterKeyEvent(0, 0, 0, VK_BACK); // Backspace
    HashTableInsert(&ht, key, Backspace_KeyEvent);

    key = RegisterKeyEvent(1, 0, 0, 'H'); // CTRL+H
    HashTableInsert(&ht, key, Backspace_KeyEvent);

    key = RegisterKeyEvent(1, 0, 0, 'D'); // CTRL+D
    HashTableInsert(&ht, key, CTRL_D_KeyEvent);

    key = RegisterKeyEvent(1, 0, 0, 'T'); // CTRL+T
    HashTableInsert(&ht, key, CTRL_T_KeyEvent);

    key = RegisterKeyEvent(1, 0, 0, 'B'); // CTRL+B
    HashTableInsert(&ht, key, CTRL_B_KeyEvent);

    key = RegisterKeyEvent(1, 0, 0, 'F'); // CTRL+F
    HashTableInsert(&ht, key, CTRL_F_KeyEvent);

    key = RegisterKeyEvent(1, 0, 0, 'N'); // CTRL+N
    HashTableInsert(&ht, key, CTRL_N_KeyEvent);

    key = RegisterKeyEvent(1, 0, 0, 'P'); // CTRL+P
    HashTableInsert(&ht, key, CTRL_P_KeyEvent);

    key = RegisterKeyEvent(0, 0, 0, VK_ESCAPE); // ESCAPE
    HashTableInsert(&ht, key, Escape_KeyEvent);

    key = RegisterKeyEvent(1, 0, 0, 'U'); // CTRL+U
    HashTableInsert(&ht, key, CTRL_U_KeyEvent);

    key = RegisterKeyEvent(1, 0, 0, 'K'); // CTRL+K
    HashTableInsert(&ht, key, CTRL_K_KeyEvent);

    key = RegisterKeyEvent(1, 0, 0, 'A'); // CTRL+A
    HashTableInsert(&ht, key, CTRL_A_KeyEvent);

    key = RegisterKeyEvent(1, 0, 0, 'E'); // CTRL+E
    HashTableInsert(&ht, key, CTRL_E_KeyEvent);

    key = RegisterKeyEvent(1, 0, 0, 'L'); // CTRL+L
    HashTableInsert(&ht, key, CTRL_L_KeyEvent);

    key = RegisterKeyEvent(1, 0, 0, 'W'); // CTRL+W
    HashTableInsert(&ht, key, CTRL_W_KeyEvent);

    key = RegisterKeyEvent(0, 0, 0, VK_UP); // UP
    HashTableInsert(&ht, key, CTRL_P_KeyEvent);

    key = RegisterKeyEvent(0, 0, 0, VK_DOWN); // DOWN
    HashTableInsert(&ht, key, CTRL_N_KeyEvent);

    key = RegisterKeyEvent(0, 0, 0, VK_LEFT); // LFT
    HashTableInsert(&ht, key, CTRL_B_KeyEvent);

    key = RegisterKeyEvent(0, 0, 0, VK_RIGHT); // RIGHT
    HashTableInsert(&ht, key, CTRL_F_KeyEvent);

    key = RegisterKeyEvent(0, 0, 0, VK_HOME); // HOME
    HashTableInsert(&ht, key, CTRL_A_KeyEvent);

    key = RegisterKeyEvent(0, 0, 0, VK_END); // End
    HashTableInsert(&ht, key, CTRL_E_KeyEvent);

    key = RegisterKeyEvent(0, 0, 0, VK_DELETE); // Delete
    HashTableInsert(&ht, key, CTRL_D_KeyEvent);
}

void linenoiseCleanup() {
    HashTableEmpty(&ht, NULL);
}
