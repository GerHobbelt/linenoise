/* linenoise.c -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * You can find the latest source code at:
 * 
 *   http://github.com/oldium/linenoise
 *
 * Does a number of crazy assumptions that happen to be true in 99.9999% of
 * the 2010 UNIX computers around.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2013, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2013-2014, Oldrich Jedlicka <oldium dot pro at seznam dot cz>
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
 * - http://github.com/antirez/linenoise
 * - http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * - http://www.3waylabs.com/nw/WWW/products/wizcon/vt220.html
 * - http://www.ecma-international.org/publications/standards/Ecma-035.htm
 * - http://www.ecma-international.org/publications/standards/Ecma-048.htm
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

#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 500
#define _BSD_SOURCE

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <locale.h>
#include <signal.h>
#include <wchar.h>
#include "linenoise.h"

#undef uchar_t
#undef char_t
#undef charpos_t
#undef unicode_t

#ifdef _WIN32
#include <io.h>

#define EWOULDBLOCK EAGAIN
#define bool int
#define true 1
#define false 0

#define uchar_t TCHAR
#define char_t TCHAR
#define charpos_t size_t
#define unicode_t __int32
#else
#include <termios.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/select.h>

#define _tcslen strlen
#define _tcsdup strdup
#define _tcscmp strcmp
#define _tcsstr strstr
#define _fgetts fgets
#define _tprintf printf
#define _ftprintf_s fprintf
#define _fgetts fgets
#define _tcschr strchr
#define _T(x) x
#define uchar_t unsigned char
#define char_t char
#define charpos_t size_t
#define unicode_t int32_t
#endif

#define RETRY(expression) \
	( { int result = (expression); while (result == -1 && errno == EINTR ) result = (expression); result; } )

#ifndef CTRL
#define CTRL(c) ((c) & 0x1f)
#endif

#ifndef CERASE
#define CERASE 127
#endif

#define CESC CTRL('[')

typedef struct linenoiseSingleCompletion {
    char_t *suggestion; /* Suggestion to display. */
    char_t *text;       /* Fully completed text. */
    charpos_t pos;      /* Cursor position. */
} linenoiseSingleCompletion;

struct linenoiseCompletions {
  bool is_initialized;  /* True if completions are initialized. */
  size_t len;           /* Current number of completions. */
  size_t max_strlen;    /* Maximum suggestion text length. */
  linenoiseSingleCompletion *cvec;  /* Array of completions. */
};

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_LINE_INIT_MAX_AND_GROW 4096
#define LINENOISE_TEMP_STRING_SIZE 8
#define LINENOISE_COL_SPACING 2
#define ANSI_ESCAPE_MAX_LEN 16
#define ANSI_ESCAPE_WAIT_MS 50  /* Wait 50ms for further ANSI codes, otherwise return escape */
#define READ_BACK_MAX_LEN 32
#define MAX_RAW_CHARS 6         /* 6 UTF-8 characters */

#define MIN(a, b) ((a) < (b) ? (a) : (b))
static linenoiseCompletionCallback *completionCallback = NULL;

#ifdef _WIN32
static DWORD orig_inconsolemode;    /* In order to restore at exit. */
static DWORD orig_outconsolemode;   /* In order to restore at exit. */
static DWORD pending_console_event[16] = {0};
static int pending_console_event_len = 0;
#else
static struct termios orig_termios; /* In order to restore at exit.*/
#endif
static int rawmode = 0; /* For atexit() function to check if restore is needed*/
static int mlmode = 0;  /* Multi line mode. Default is single line. */
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char_t **history = NULL;

enum LinenoiseState {
    LS_NEW_LINE,        /* Processing new line. */
    LS_READ,            /* Reading new line. */
    LS_COMPLETION,      /* Completing with TAB. */
    LS_HISTORY_SEARCH   /* Searching with CTRL+R. */
};

enum ReadCharSpecials {
    RCS_NONE = 0,               /* No character read. */
    RCS_ERROR = -1,             /* Error. */
    RCS_CLOSED = -2,            /* Connection has been closed. */
    RCS_CANCELLED = -3,         /* Line editing has been cancelled. */
    RCS_CURSOR_LEFT = -4,       /* Left key. */
    RCS_CURSOR_RIGHT = -5,      /* Right key. */
    RCS_CURSOR_UP = -6,         /* Up key. */
    RCS_CURSOR_DOWN = -7,       /* Down key. */
    RCS_DELETE = -8,            /* Delete key. */
    RCS_HOME = -9,              /* Home key. */
    RCS_END = -10               /* End key. */
};

enum AnsiEscapeState {
    AES_NONE = 0,               /* Not in escape sequence. */
    AES_INTERMEDIATE = 1,       /* Intermediate sequence. */
    AES_CSI_PARAMETER = 2,      /* Inside CSI parameter sequence. */
    AES_CSI_INTERMEDIATE = 3,   /* Inside CSI intermediate sequence. */
    AES_SS_CHARACTER = 4,       /* Reading SS2 or SS3 character value. */
    AES_FINAL = 5               /* Read final character. */
};

enum AnsiSequenceMeaning {
    ASM_C1,     /** Read C1 escape sequence. */
    ASM_CSI,    /** Read CSI escape sequence. */
    ASM_G2,     /** Read SS2 escape and a character value. */
    ASM_G3      /** Read SS3 escape and a character value. */
};

enum LinenoiseResult {
    LR_HAVE_TEXT = 1,   /** Text is available to be returned. */
    LR_CLOSED = 0,      /** Connection has been closed with no text to be returned. */
    LR_ERROR = -1,      /** Error occurred. */
    LR_CANCELLED = -2,  /** Current line editing has been cancelled. */
    LR_CONTINUE = -3    /** Continue reading next character. */
};

typedef struct linenoiseString {
    char_t *buf;            /* String buffer. */
    charpos_t *charindex;   /* Starting position of characters. */
    size_t buflen;          /* String buffer size. */
    size_t bytelen;         /* String length (in bytes). */
    size_t charlen;         /* String length (in characters). */
} linenoiseString;

typedef struct linenoiseChar {
    unicode_t unicodeChar;
    uchar_t rawChars[MAX_RAW_CHARS+1];
    size_t rawCharsLen;
} linenoiseChar;

typedef struct linenoiseRawChar {
    linenoiseChar currentChar;
#ifdef _WIN32
    linenoiseString tempString;
#endif
    bool is_emited;
    mbstate_t readingState;
} linenoiseRawChar;

#ifndef _WIN32
typedef struct linenoiseAnsi {
    linenoiseChar escape;       /* Read escape character. */
    timer_t ansi_timer;         /* Timer to differentiate between ESC and ANSI escape sequence. */
    bool ansi_timer_created;    /* True whether the timer has been created */
    bool ansi_timer_is_active;  /* True if the timer is active. */
    int ansi_timer_overrun_count;   /* Overrun count of timer. */
    enum AnsiEscapeState ansi_state;    /* ANSI sequence reading state */
    struct linenoiseChar ansi_escape[ANSI_ESCAPE_MAX_LEN + 1];  /* RAW read ANSI escape sequence */
    char_t ansi_intermediate[ANSI_ESCAPE_MAX_LEN + 1];    /* Intermediate sequence. */
    char_t ansi_parameter[ANSI_ESCAPE_MAX_LEN + 1];   /* Parameter sequence. */
    char_t ansi_final;    /* Final character of sequence. */
    enum AnsiSequenceMeaning ansi_sequence_meaning;   /* Read sequence meaning. */
    int ansi_escape_len;                    /* Current length of sequence */
    int ansi_intermediate_len;              /* Current length of intermediate block */
    int ansi_parameter_len;                 /* Current length of parameter block */
    linenoiseChar temp_char;    /* Temporary character for negative RCS_* codes. */
} linenoiseAnsi;
#endif

typedef struct linenoiseHistorySearchState {
    struct linenoiseString text;    /* Text to be searched. */
    int current_index;              /* Current history index. */
    bool found;                     /* True if current text has been fouond. */
} linenoiseHistorySearchState;

/* The linenoiseState structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct linenoiseState {
    enum LinenoiseState state;  /* Internal state. */
#ifdef _WIN32
    HANDLE fdin;        /* Terminal file descriptor. */
    HANDLE fdout;       /* Terminal file descriptor. */
    HANDLE wakeup_event;    /* Wake-up event for console signals. */
#else
    int fd;             /* Terminal file descriptor. */
#endif
    bool is_supported;  /* True if the terminal is supported. */
    linenoiseString line;   /* Current edited line. */
    linenoiseString prompt; /* Prompt to display. */
    linenoiseString tempprompt;   /* Temporary prompt to display. */
    size_t pos;         /* Current cursor position. */
    size_t oldpos;      /* Previous refresh cursor position. */
    size_t oldrpos;     /* Previous cursor row position. */
    size_t cols;        /* Number of columns in terminal. */
#ifdef _WIN32
    size_t rows;        /* Number of rows in terminal. */
#endif
    size_t maxrows;     /* Maximum num of rows used so far (multiline mode) */
    int history_index;  /* The history index we are currently editing. */
    bool is_async;      /* True when the STDIN is in O_NONBLOCK mode. */
    bool needs_refresh; /* True when the lines need to be refreshed. */
    bool is_displayed;  /* True when the prompt has been displayed. */
    bool is_cancelled;  /* True when the input has been cancelled (CTRL+C). */
    bool is_closed;     /* True once the input has been closed. */
    linenoiseCompletions comp;  /* Line completions. */
    bool signals_blocked;   /* True when the SIGINT is blocked. */
#ifndef _WIN32
    sigset_t sigint_oldmask;    /* Old signal mask. */
#endif
	linenoiseRawChar rawChar;   /* Character reading state. */
#ifndef _WIN32
    linenoiseAnsi ansi; /* ANSI escape sequence state machine. */
#endif
	linenoiseChar read_back_char[READ_BACK_MAX_LEN];    /* Read-back buffer for characters. */
    linenoiseChar read_back_return; /* Read-back character to be returned (not further processed). */
    linenoiseChar cached_read_char; /* Cached lastly read character to be processed. */
    int read_back_char_len;                     /* Number of characters in buffer. */
    linenoiseHistorySearchState hist_search;    /* History search. */
};

static struct linenoiseState state = {      /* Line editing state. */
        LS_NEW_LINE
};
static volatile bool initialized = false;        /* True if line editing has been initialized. */

static const linenoiseChar CHAR_NONE = { RCS_NONE, {0}, 0 };
static const linenoiseChar CHAR_ERROR = { RCS_ERROR, {0}, 0 };
static const linenoiseChar CHAR_CANCELLED = { RCS_CANCELLED, {0}, 0 };

static void linenoiseAtExit(void);
static int refreshLine(struct linenoiseState *l);
static int ensureInitialized(struct linenoiseState *l);
static int initialize(struct linenoiseState *l);
static void updateSize();
static int ensureBufLen(struct linenoiseString *s, size_t requestedBufLen);
static int prepareCustomOutputOnNewLine(struct linenoiseState *l);
static int prepareCustomOutputClearLine(struct linenoiseState *l);
static int freeHistorySearch(struct linenoiseState *l);

/* ======================= Line and buffer manipulation ===================== */

/* Fill character representation 'tempChar' of unicode char 'cs'.
 * Returns supplied 'tempChar'. */
static const struct linenoiseChar *getChar(struct linenoiseChar *tempChar, unicode_t cs)
{
    int saved_errno = errno;
    tempChar->unicodeChar = cs;
    if (cs > 0) {
#if !defined(_WIN32)
        mbstate_t state;
        memset(&state, 0, sizeof(state));
        tempChar->rawCharsLen = wcrtomb((char_t*)tempChar->rawChars, cs, &state);
        if (tempChar->rawCharsLen == (size_t)-1 || tempChar->rawCharsLen == 0) {
            tempChar->unicodeChar = RCS_NONE;
            tempChar->rawCharsLen = 0;
        }
#else   /* _WIN32 && _UNICODE */
        int convertedLen = 0;
        wchar_t converted[3] = { 0, 0, 0 };
        if (cs < 0x010000) {
            convertedLen = 1;
            converted[0] = (TCHAR)cs;
        } else if (cs <= 0x10FFFF) {
            unicode_t to_encode = cs - 0x10000;
            convertedLen = 2;
            converted[0] = (TCHAR)(cs >> 10) + 0xD7C0;
            converted[1] = (TCHAR)(cs & 0x3FF) + 0xDC00;
        } else {
            convertedLen = 0;
        }
#if !defined(_UNICODE)
        if (convertedLen > 0) {
            tempChar->rawCharsLen = WideCharToMultiByte(CP_ACP, 0,
                converted, convertedLen, tempChar->rawChars,
                MAX_RAW_CHARS, NULL, NULL);
        } else {
            tempChar->rawCharsLen = 0;
        }
#else
        memcpy(tempChar->rawChars, converted, convertedLen*sizeof(*converted));
        tempChar->rawCharsLen = convertedLen;
#endif
#endif
    } else {
        tempChar->rawCharsLen = 0;
    }
    errno = saved_errno;
    return tempChar;
}

/* Returns pointer to character at a character position. */
static char_t *getCharAt(struct linenoiseString *s, size_t pos)
{
    if (pos >= s->charlen) {
        return s->buf + s->charindex[s->charlen];
    } else {
        return s->buf + s->charindex[pos];
    }
}

#ifdef _WIN32
static unicode_t fromUtf16(wchar_t first, wchar_t second)
{
    if (first < 0xD800 || first > 0xDFFF) {
        return (unicode_t)first;
    } else if (first <= 0xDBFF && second >= 0xDC00 && second <= 0xDFFF) {
        return ((first & 0x03FF) << 10) + (second & 0x03FF);
    } else
        return 0;
}
#endif

/* Returns unicode character at a supplied position. */
static unicode_t getUnicodeCharAt(struct linenoiseString *s, size_t pos)
{
#if !defined(_WIN32)
    wchar_t c = 0;
    int saved_errno = errno;
    mbstate_t state;
    memset(&state, 0, sizeof(state));
    (void) mbrtowc(&c, getCharAt(s, pos), 1, &state);
    errno = saved_errno;
    return c;
#else   /* _WIN32 */
#if !defined(_UNICODE)
    WCHAR wide[2] = { 0, 0 };
    wchar_t *start = wide;
    wchar_t *end = wide + 2;
    if (MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, getCharAt(s, pos), 1, (LPWSTR)&wide, 2) == 0)
        return 0;
#else   /* _WIN32 && _UNICODE */
    wchar_t *start = getCharAt(s, pos);
    wchar_t *end = getCharAt(s, pos+1);
#endif
    return fromUtf16(*start, start+1 < end ? *(start+1) : 0);
#endif  /* _WIN32 */
}

/* Returns multi-byte character size at a supplied position. */
static size_t getCharSizeAt(struct linenoiseString *s, size_t pos)
{
    if (pos >= s->charlen) {
        return 0;
    } else {
        return s->charindex[pos + 1] - s->charindex[pos];
    }
}

/* Check if the byte position is at character boundary. */
static bool isAtCharBoundary(size_t pos, struct linenoiseString *s)
{
    size_t i;
    for (i = 0; i <= s->charlen; i++) {
        if (pos == s->charindex[i]) {
            return true;
        }
    }
    return false;
}

/* Find nearest character index from byte position. */
static size_t findNearestCharIndex(size_t pos, struct linenoiseString *s)
{
    size_t i;
    for (i = 0; i <= s->charlen; i++) {
        if (pos <= s->charindex[i]) {
            return i;
        }
    }
    return s->charlen;
}

/* Clear linenoiseString (set width to 0). */
static void clearString(struct linenoiseString *s)
{
    s->bytelen = s->charlen = 0;
    if (s->buf != NULL)
        s->buf[0] = '\0';
}

/* Free lineoiseString. */
static void freeString(struct linenoiseString *s)
{
    free(s->buf);
    free(s->charindex);
    s->buf = NULL;
    s->charindex = NULL;
    s->bytelen = s->charlen = s->buflen = 0;

}

/* ======================= Low level terminal handling ====================== */

/* Set if to use or not the multi line mode. */
void linenoiseSetMultiLine(int ml) {
    mlmode = ml;
}

/* Return true if the terminal is not a TTY or the name is in the list of
 * terminals we know are not able to understand basic escape sequences. */
static int isUnsupportedTerm(struct linenoiseState *l) {
#ifdef _WIN32
    if (GetFileType(l->fdin) == FILE_TYPE_CHAR && GetFileType(l->fdout) == FILE_TYPE_CHAR) return 0;
    return 1;
#else
    static char *unsupported_term[] = {"dumb","cons25",NULL};
    char *term;
    int j;

    if ( !isatty(l->fd) )
        return 1;

    term = getenv("TERM");

    if (term == NULL) return 0;
    for (j = 0; unsupported_term[j]; j++)
        if (!strcasecmp(term,unsupported_term[j])) return 1;
    return 0;
#endif
}

/* Raw mode: 1960 magic shit. */
static int enableRawMode(struct linenoiseState *l) {
    if (!rawmode) {
#ifdef _WIN32
        if (GetConsoleMode(l->fdin, &orig_inconsolemode))
            SetConsoleMode(l->fdin, ENABLE_PROCESSED_INPUT | ENABLE_WINDOW_INPUT);
        if (GetConsoleMode(l->fdout, &orig_outconsolemode) && !mlmode)
            SetConsoleMode(l->fdout, orig_outconsolemode & ~ENABLE_WRAP_AT_EOL_OUTPUT);
#else
        struct termios raw;

        if (tcgetattr(l->fd,&orig_termios) == -1) goto fatal;

        raw = orig_termios;  /* modify the original mode */
        /* input modes: no CR to NL, no parity check, no strip char,
         * no start/stop output control. */
        raw.c_iflag &= ~(ICRNL | INLCR | INPCK | ISTRIP | IXON);
        /* output modes - disable post processing */
        raw.c_oflag &= ~(OPOST);
        /* control modes - set 8 bit chars */
        raw.c_cflag |= (CS8);
        /* local modes - choing off, canonical off, no extended functions,
         * no signal chars (^Z,^C) */
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
        /* control chars - set return condition: min number of bytes and timer.
         * We want read to return every single byte, without timeout. */
        raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

        /* put terminal in raw mode after flushing */
        if (tcsetattr(l->fd,TCSAFLUSH,&raw) < 0) goto fatal;
#endif
        rawmode = 1;
    }
    return 0;

#ifndef _WIN32
fatal:
    errno = ENOTTY;
    return -1;
#endif
}

/* Disable raw mode. */
static void disableRawMode(struct linenoiseState *l) {
    int saved_errno = errno;
    /* Don't even check the return value as it's too late. */
    if (rawmode) {
#ifdef _WIN32
        SetConsoleMode(l->fdin, orig_inconsolemode);
        SetConsoleMode(l->fdout, orig_outconsolemode);
#else
        if (tcsetattr(l->fd,TCSAFLUSH,&orig_termios) != -1)
            rawmode = 0;
#endif
    }
    errno = saved_errno;
}

/* Try to get the number of columns in the current terminal, or assume 80
 * if it fails. */
static bool setSize(struct linenoiseState *l) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    bool changed;
    if (!GetConsoleScreenBufferInfo(l->fdout, &info)) {
        info.dwSize.X = 80;
        info.dwSize.Y = 25;
    }
    changed = l->cols != info.dwSize.X || l->rows != info.dwSize.Y;
    l->cols = info.dwSize.X;
    l->rows = info.dwSize.Y;
    return changed;
#else
    struct winsize ws;
    bool changed;

    if (RETRY(ioctl(1, TIOCGWINSZ, &ws)) == -1 || ws.ws_col == 0)
        ws.ws_col = 80;
    changed = l->cols != ws.ws_col;
    l->cols = ws.ws_col;
    return changed;
#endif
}

#ifdef _WIN32
static BOOL WINAPI console_handler(DWORD win_event)
{
    if (win_event == CTRL_C_EVENT || win_event == CTRL_BREAK_EVENT) {
        if (pending_console_event_len <
                sizeof(pending_console_event)/sizeof(*pending_console_event)) {
            pending_console_event[pending_console_event_len++] = win_event;
        }
        SetEvent(state.wakeup_event);
        return TRUE;
    } else {
        SetEvent(state.wakeup_event);
        return FALSE;
    }
}
#endif

/* Block SIGINT, SIGALRM and SIGWINCH signals. */
static bool blockSignals(struct linenoiseState *ls) {
    if (!ls->signals_blocked) {
        int old_errno = errno;
#ifdef _WIN32
        SetConsoleCtrlHandler(console_handler, TRUE);
#else
        sigset_t newset;
        sigemptyset(&newset);
        sigemptyset(&ls->sigint_oldmask);
        sigaddset(&newset, SIGINT);
        sigaddset(&newset, SIGALRM);
        sigaddset(&newset, SIGWINCH);
        pthread_sigmask(SIG_BLOCK, &newset, &ls->sigint_oldmask);
#endif
        ls->signals_blocked = true;
        errno = old_errno;
        return true;
    } else {
        return false;
    }
}

/* Re-enable SIGINT, SIGALRM and SIGWINCH signals. */
static bool revertSignals(struct linenoiseState *ls) {
    if (ls->signals_blocked) {
        int old_errno = errno;
#ifdef _WIN32
        int i;
        SetConsoleCtrlHandler(console_handler, FALSE);
        for (i = 0; i < pending_console_event_len; i++)
            GenerateConsoleCtrlEvent(pending_console_event[i], 0);
        pending_console_event_len = 0;
#else
        pthread_sigmask(SIG_SETMASK, &ls->sigint_oldmask, NULL);
#endif
        ls->signals_blocked = false;
        errno = old_errno;
        return true;
    } else
        return false;
}

/* Clear the screen. Used to handle ctrl+l */
int clearScreen(struct linenoiseState *ls) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    COORD topleft = { 0, 0 };
	DWORD n;
    if (!GetConsoleScreenBufferInfo(ls->fdout, &info)) return -1;
    FillConsoleOutputCharacter(ls->fdout, ' ', (DWORD)(ls->cols * ls->rows), topleft, &n);
    FillConsoleOutputAttribute(ls->fdout,
        FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN, (DWORD)(ls->cols * ls->rows),
        topleft, &n);
    SetConsoleCursorPosition(ls->fdout, topleft);
#else
    if (RETRY(write(ls->fd,"\x1b[H\x1b[2J",7)) <= 0) {
        /* nothing to do, just to avoid warning. */
    }
#endif
    ls->needs_refresh = true;
    ls->maxrows = 0;
    return 0;
}

/* Clear the screen. Used to handle ctrl+l */
int linenoiseClearScreen(void) {
    return clearScreen(&state);
}

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
static int linenoiseBeep(void) {
#ifdef _WIN32
    Beep(800, 200);
#else
    if (fprintf(stderr, "\x7") < 0) return -1;
    if (RETRY(fflush(stderr)) == -1) return -1;
#endif
    return 0;
}

static int cursorMoveLeft(struct linenoiseState *l)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(l->fdout, &info)) {
        COORD pos = {0, info.dwCursorPosition.Y};
        SetConsoleCursorPosition(l->fdout, pos);
    }
    return 0;
#else
    const char* seq = "\x1b[1G";
    if (RETRY(write(l->fd,seq,strlen(seq))) == -1) return -1;
#endif
    return 0;
}

static int cursorSetColumn(struct linenoiseState *l, size_t column)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(l->fdout, &info)) {
        COORD pos = {(SHORT)column, info.dwCursorPosition.Y};
        SetConsoleCursorPosition(l->fdout, pos);
    }
#else
    char seq[64];
    if (snprintf(seq,64,"\x1b[%zuG", column+1) < 0) return -1;
    if (RETRY(write(l->fd,seq,strlen(seq))) == -1) return -1;
#endif
    return 0;
}

static int cursorMoveDown(struct linenoiseState *l, size_t rows)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(l->fdout, &info)) {
        COORD pos = {info.dwCursorPosition.X, (SHORT)(info.dwCursorPosition.Y + rows)};
        if (pos.Y >= info.dwMaximumWindowSize.Y)
            pos.Y = info.dwMaximumWindowSize.Y - 1;
        SetConsoleCursorPosition(l->fdout, pos);
    }
#else
    char seq[64];
    if (snprintf(seq,64,"\x1b[%zuB", rows) < 0) return -1;
    if (RETRY(write(l->fd,seq,strlen(seq))) == -1) return -1;
#endif
    return 0;
}

static int cursorMoveUp(struct linenoiseState *l, size_t rows)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(l->fdout, &info)) {
        COORD pos = {info.dwCursorPosition.X, (SHORT)(info.dwCursorPosition.Y - rows)};
        if (pos.Y < 0)
            pos.Y = 0;
        SetConsoleCursorPosition(l->fdout, pos);
    }
#else
    char seq[64];
    if (snprintf(seq,64,"\x1b[%zuA", rows) < 0) return -1;
    if (RETRY(write(l->fd,seq,strlen(seq))) == -1) return -1;
#endif
    return 0;
}

static int eraseLineEnd(struct linenoiseState *l)
{
#ifdef _WIN32
    DWORD oldmode = (DWORD)-1;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleMode(l->fdout, &oldmode))
        SetConsoleMode(l->fdout, oldmode & ~ENABLE_WRAP_AT_EOL_OUTPUT);
    if (GetConsoleScreenBufferInfo(l->fdout, &info)) {
        DWORD written = 0;
        FillConsoleOutputCharacter(l->fdout, _T(' '),
            info.dwMaximumWindowSize.X-info.dwCursorPosition.X, info.dwCursorPosition, &written);
        SetConsoleCursorPosition(l->fdout, info.dwCursorPosition);
    }

    if (oldmode != (DWORD)-1)
        SetConsoleMode(l->fdout, oldmode);
#else
    const char* seq = "\x1b[0K";
    if (RETRY(write(l->fd,seq,strlen(seq))) == -1) return -1;
#endif
    return 0;
}

static int eraseLineAndMoveUp(struct linenoiseState *l)
{
#ifdef _WIN32
    if (cursorSetColumn(l, 0) == -1) return -1;
    if (eraseLineEnd(l) == -1) return -1;
    if (cursorMoveUp(l, 1) == -1) return -1;
#else
    char seq[64];
    if (snprintf(seq,64,"\x1b[0G\x1b[0K\x1b[1A") < 0) return -1;
    if (RETRY(write(l->fd,seq,strlen(seq))) == -1) return -1;
#endif
    return 0;
}

static int writeChar(struct linenoiseState *l, const struct linenoiseChar *c)
{
#ifdef _WIN32
    WriteConsole(l->fdout, c->rawChars, (DWORD)c->rawCharsLen, NULL, NULL);
#else
    if (RETRY(write(l->fd, c->rawChars, c->rawCharsLen * sizeof(char_t))) == -1) return -1;
#endif
    return 0;
}

static int writeLine(struct linenoiseState *l, struct linenoiseString *s, size_t pos, size_t count)
{
    char_t *buf = getCharAt(s, pos);
    size_t end_pos;
    size_t len;
#ifdef _WIN32
    DWORD written;
#endif
    if (pos+count < pos) end_pos = SIZE_MAX;
    else end_pos = pos + count;
    len = getCharAt(s, end_pos) - buf;
#ifdef _WIN32
    WriteConsole(l->fdout, buf, (DWORD)len, &written, NULL);
#else
    if (RETRY(write(l->fd, buf, len * sizeof(char_t))) == -1) return -1;
#endif
    return 0;
}

/* ============================== Completion ================================ */

/* Free a list of completion option populated by linenoiseAddCompletion(). */
static void freeCompletions(struct linenoiseState *ls) {
    size_t i;
    if (ls->comp.cvec != NULL) {
        for (i = 0; i < ls->comp.len; i++) {
            free(ls->comp.cvec[i].suggestion);
            free(ls->comp.cvec[i].text);
        }
        free(ls->comp.cvec);
    }

    ls->comp.is_initialized = false;
    ls->comp.cvec = NULL;
    ls->comp.len = 0;
    ls->comp.max_strlen = 0;
}

/* Compare completions, used for Quick sort. */
static int completitionCompare(const void *first, const void *second)
{
    linenoiseSingleCompletion *firstcomp = (linenoiseSingleCompletion *) first;
    linenoiseSingleCompletion *secondcomp = (linenoiseSingleCompletion *) second;
#if !defined(WIN32) || defined(_WIN32) && !defined(_UNICODE)
    return (strcoll(firstcomp->suggestion, secondcomp->suggestion));
#else
    return (wcscoll(firstcomp->suggestion, secondcomp->suggestion));
#endif
}

/* Parse line into linenoiseString. */
int parseLine(const char_t *src, size_t srclen, struct linenoiseString *dest)
{
    int saved_errno = errno;
    const char_t *charstart = src;
    const char_t *charend = src;
    const char_t *srcend = src + srclen;
    size_t newbytelen = 0;
    size_t newcharlen = 0;
    mbstate_t state;

    if (ensureBufLen(dest, srclen+1) == -1) return -1;

    memset(&state, 0, sizeof(state));

    dest->charindex[0] = 0;
    while (charend < srcend) {
#if !defined(_WIN32)
        int found = mbrlen(charend, 1, &state);
#elif !defined(_UNICODE)
        int found = 1;
#else   /* _WIN32 && _UNICODE */
        int found;
        if (*charstart < 0xD800 || *charstart > 0xDFFF) {
            found = 1;
        } else if (*charstart <= 0xDBFF && charstart+1 < srcend
                && *(charstart+1) >= 0xDC00 && *(charstart+1) <= 0xDFFF) {
            charend++;
            found = 1;
        } else if (charstart+1 < srcend) {
            ++charend;
            found = (size_t)-1; /* Invalid sequence. */
        } else {
            found = (size_t)-2; /* Incomplete sequence. */
        }
#endif
        if (found == (size_t)-1) {
            charstart = charend;
            continue;
        } else if (found != (size_t)-2) {
            size_t newCharSize = (charend - charstart) + 1;
            memcpy(dest->buf+newbytelen, charstart, newCharSize*sizeof(char_t));
            newcharlen++;
            newbytelen += newCharSize;
            dest->charindex[newcharlen] = newbytelen;
            charstart = ++charend;
        } else {
            ++charend;
        }
    }
    dest->buf[newbytelen] = '\0';
    dest->charlen = newcharlen;
    dest->bytelen = newbytelen;
    errno = saved_errno;
    return 0;
}

/* Replace line with unparsed text (which is parsed). */
static int replaceLine(struct linenoiseState *l, char_t *text, size_t len)
{
    if (ensureBufLen(&l->line, len+1) == -1) return -1;
    parseLine(text, len, &l->line);
    l->pos = l->line.charlen;
    return 0;
}

/* This is an helper function for linenoiseEdit() and is called when the
 * user types the <tab> key in order to complete the string currently in the
 * input.
 * 
 * The state of the editing is encapsulated into the pointed linenoiseState
 * structure as described in the structure definition. */
static int completeLine(struct linenoiseState *ls) {
    if (ls->comp.len == 0) {
        if (linenoiseBeep() == -1) return -1;
        if (ls->needs_refresh) {
            if (refreshLine(ls) == -1) return -1;
        }
    } else if (ls->comp.len == 1) {
        /* Simple case */
        size_t new_strlen = _tcslen(ls->comp.cvec[0].text);
        if (replaceLine(ls, ls->comp.cvec[0].text, new_strlen) == -1) return -1;
        ls->pos = findNearestCharIndex(ls->comp.cvec[0].pos, &ls->line);
        if (refreshLine(ls) == -1) return -1;
    } else {
        size_t colSize;
        size_t cols;
        size_t rows;
        size_t i;

        /* Multiple choices - sort them and print */
        if (prepareCustomOutputOnNewLine(ls) == -1) return -1;

        qsort(ls->comp.cvec, ls->comp.len, sizeof(linenoiseSingleCompletion), completitionCompare);
        colSize = ls->comp.max_strlen + LINENOISE_COL_SPACING;
        cols = ls->cols / colSize;
        if (cols == 0)
            cols = 1;
        rows = (ls->comp.len + cols - 1) / cols;

        for (i = 0; i < ls->comp.len; i++)
        {
            size_t real_index = (i % cols) * rows + i / cols;
            if (real_index < ls->comp.len)
                _tprintf(_T("%-*s"), (int)colSize, ls->comp.cvec[real_index].suggestion);
            if ((i % cols) == (cols - 1))
                _tprintf(_T("\r\n"));
        }
        if ((i % cols) != 0)
            _tprintf(_T("\r\n"));
        if (refreshLine(ls) == -1) return -1;
    }

    return 0;
}

/* Register a callback function to be called for tab-completion. */
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn) {
    completionCallback = fn;
}

/* This function is used by the callback function registered by the user
 * in order to add completion options given the input string when the
 * user typed <tab>. See the example.c source code for a very easy to
 * understand example. */
int linenoiseAddCompletion(linenoiseCompletions *lc, const char_t *suggestion, const char_t *completed_text, charpos_t pos) {
    int result = 0;
    size_t len;
    size_t i;
    char_t *copy_suggestion = NULL;
    char_t *copy_text = NULL;

    if (suggestion == NULL || completed_text == NULL || *suggestion == '\0' || *completed_text == '\0') {
        errno = EINVAL;
        return -1;
    }

    len = _tcslen(suggestion);
    copy_suggestion = malloc((len+1)*sizeof(char_t));
    copy_text = _tcsdup(completed_text);
    if (copy_suggestion == NULL || copy_text == NULL) goto error_cleanup;
    memcpy(copy_suggestion,suggestion,(len+1)*sizeof(char_t));
    if (lc->len == 0 || lc->cvec != NULL) {
        linenoiseSingleCompletion *newcvec = realloc(lc->cvec,sizeof(linenoiseSingleCompletion)*(lc->len+1));;
        if (newcvec == NULL) goto error_cleanup;
        lc->cvec = newcvec;
        lc->cvec[lc->len].suggestion = copy_suggestion;
        lc->cvec[lc->len].text = copy_text;
        lc->cvec[lc->len].pos = pos;
    }
    goto end;

error_cleanup:
    free(copy_suggestion);
    free(copy_text);

    for (i = 0; i < lc->len; i++)
        free(lc->cvec[i].text);
    free(lc->cvec);
    lc->cvec = NULL;
    errno = ENOMEM;
    result = -1;

end:
    if (len > lc->max_strlen)
        lc->max_strlen = len;
    lc->len++;
    return result;
}

/* =========================== Line editing ================================= */

/* Get the prompt to be displayed. */
linenoiseString *getPrompt(struct linenoiseState *l)
{
    if (l->tempprompt.buf != NULL) {
        return &l->tempprompt;
    } else {
        return &l->prompt;
    }
}

/* Set the main prompt. */
int setPrompt(struct linenoiseState *l, const char_t *prompt)
{
    if (l->is_supported) {
        size_t promptLen = prompt != NULL ? _tcslen(prompt) : 0;
        linenoiseString newPrompt = {NULL, NULL, 0, 0, 0};
        linenoiseString *oldPrompt;

        if (prompt != NULL) {
            if (parseLine(prompt, promptLen, &newPrompt) == -1) return -1;
        }

        oldPrompt = getPrompt(l);
        if ((oldPrompt->buf == NULL && newPrompt.buf != NULL)
                || (oldPrompt->buf != NULL && newPrompt.buf == NULL)
                || (oldPrompt->buf != NULL && newPrompt.buf != NULL
                        && _tcscmp(oldPrompt->buf, newPrompt.buf) != 0))
            l->needs_refresh = true;

        if (l->prompt.buf != NULL) freeString(&l->prompt);
        l->prompt = newPrompt;
    } else {
        if ((l->prompt.buf != NULL && prompt == NULL)
                || (l->prompt.buf == NULL && prompt != NULL)
                || (l->prompt.buf != NULL && prompt != NULL
                        && _tcscmp(l->prompt.buf, prompt) != 0)) {
            free(l->prompt.buf);
            l->prompt.buf = NULL;
            if (prompt != NULL) {
                l->prompt.buf = _tcsdup(prompt);
                if (l->prompt.buf == NULL) {
                    errno = ENOMEM;
                    return -1;
                }
            }
        }
    }
    return 0;
}

/* Set or reset (in case of NULL argument) the temporary prompt, which has
 * priority over the main prompt. */
int setTempPrompt(struct linenoiseState *l, const char_t *tempprompt)
{
    size_t promptLen = tempprompt != NULL ? _tcslen(tempprompt) : 0;
    linenoiseString newPrompt = {NULL, NULL, 0, 0, 0};
    linenoiseString *oldPrompt;

    if (tempprompt != NULL) {
        if (parseLine(tempprompt, promptLen, &newPrompt) == -1) return -1;
    }

    oldPrompt = getPrompt(l);
    if ((oldPrompt->buf == NULL && newPrompt.buf != NULL)
            || (oldPrompt->buf != NULL && newPrompt.buf == NULL)
            || (oldPrompt->buf != NULL && newPrompt.buf != NULL
                    && _tcscmp(oldPrompt->buf, newPrompt.buf) != 0))
        l->needs_refresh = true;

    if (l->tempprompt.buf != NULL) freeString(&l->tempprompt);
    l->tempprompt = newPrompt;
    return 0;
}

/* Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static int refreshSingleLine(struct linenoiseState *l) {
    linenoiseString *prompt = getPrompt(l);
    size_t charpos = 0;
    size_t charlen = l->line.charlen;
    size_t pos = l->pos;
    
    while((prompt->charlen+pos) >= l->cols) {
        charpos++;
        charlen--;
        pos--;
    }

    while (prompt->charlen+charlen > l->cols) {
        charlen--;
    }

    /* Cursor to left edge */
    if (cursorMoveLeft(l) == -1) return -1;
    /* Write the prompt and the current buffer content */
    if (prompt->buf != NULL && prompt->bytelen > 0 && writeLine(l, prompt, 0, SIZE_MAX) == -1) return -1;
    if (writeLine(l, &l->line, charpos, charlen) == -1) return -1;
#ifdef _WIN32
    // Workaround for wrong cursor movement.
    if (charlen+prompt->charlen != l->cols) {
        /* Erase to right */
        if (eraseLineEnd(l) == -1) return -1;
    }
    /* Move cursor to original position. */
    if (pos+prompt->charlen != 0) {
        if (cursorSetColumn(l, pos+prompt->charlen) == -1) return -1;
    }
#else
    /* Erase to right */
    if (eraseLineEnd(l) == -1) return -1;
    /* Move cursor to original position. */
    if (pos+prompt->charlen != 0) {
        if (cursorSetColumn(l, pos+prompt->charlen) == -1) return -1;
    }
#endif
    return 0;
}

/* Multi-line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static int refreshMultiLine(struct linenoiseState *l) {
    linenoiseString *prompt = getPrompt(l);
    size_t rows = (prompt->charlen+l->line.charlen+l->cols-1)/l->cols; /* rows used by current buf. */
    size_t rpos = l->oldrpos;  /* cursor relative row. */
    size_t rpos2; /* rpos after refresh. */
    size_t old_rows = l->maxrows;
    size_t j;

    /* Update maxrows if needed. */
    if (rows > (int)l->maxrows) l->maxrows = rows;

#ifdef LN_DEBUG
    FILE *fp = fopen("/tmp/debug.txt","a");
    fprintf(fp,"[%d %d %d] p: %d, rows: %d, rpos: %d, max: %d, oldmax: %d",
        (int)l->len,(int)l->pos,(int)l->oldpos,plen,rows,rpos,(int)l->maxrows,old_rows);
#endif

    /* First step: clear all the lines used before. To do so start by
     * going to the last row. */
    if (old_rows > rpos) {
#ifdef LN_DEBUG
        fprintf(fp,", go down %d", old_rows-rpos);
#endif
        cursorMoveDown(l, old_rows-rpos);
    }

    /* Now for every row clear it, go up. */
    if (old_rows > 1) {
        for (j = 0; j < old_rows-1; j++) {
#ifdef LN_DEBUG
            fprintf(fp,", clear+up");
#endif
            if (eraseLineAndMoveUp(l) == -1) return -1;
        }
    }

    /* Clean the top line. */
#ifdef LN_DEBUG
    fprintf(fp,", clear");
#endif
    if (cursorMoveLeft(l) == -1) return -1;
    if (eraseLineEnd(l) == -1) return -1;
    
    /* Write the prompt and the current buffer content */
    if (prompt->buf != NULL && prompt->bytelen > 0 && writeLine(l,prompt,0,SIZE_MAX) == -1) return -1;

#ifdef _WIN32
    if (prompt->charlen+l->line.charlen > 0 &&
        (prompt->charlen+l->line.charlen) % l->cols == 0) {
        if (writeLine(l,&l->line,0,l->line.charlen-1) == -1) return -1;
        SetConsoleMode(l->fdout, orig_outconsolemode & ~ENABLE_WRAP_AT_EOL_OUTPUT);
        if (writeLine(l,&l->line,l->line.charlen-1, 1) == -1) return -1;
        SetConsoleMode(l->fdout, orig_outconsolemode);
    }
    else
#endif
        if (writeLine(l,&l->line,0,SIZE_MAX) == -1) return -1;

    /* If we are at the very end of the screen with our prompt, we need to
     * emit a newline and move the prompt to the first column. */
    if (l->pos &&
        l->pos == l->line.charlen &&
        (l->pos+prompt->charlen) > 0 &&
        (l->pos+prompt->charlen) % l->cols == 0)
    {
#ifdef LN_DEBUG
        fprintf(fp,", <newline>");
#endif

#ifdef _WIN32
        if (!WriteConsole(l->fdout, _T("\n"), 1, NULL, NULL)) return -1;
#else
        if (RETRY(write(l->fd,"\n",1)) == -1) return -1;
#endif
        if (cursorSetColumn(l, 0) == -1) return -1;

        rows++;
        if (rows > (int)l->maxrows) l->maxrows = rows;
    }

    /* Move cursor to right position. */
    rpos2 = (prompt->charlen+l->pos+l->cols)/l->cols; /* current cursor relative row. */
#ifdef LN_DEBUG
    fprintf(fp,", rpos2 %d", rpos2);
#endif
    /* Go up till we reach the expected positon. */
    if (rows > rpos2) {
#ifdef LN_DEBUG
        fprintf(fp,", go-up %d", rows-rpos2);
#endif
        if (cursorMoveUp(l, rows-rpos2) == -1) return -1;
    }
    /* Set column. */
#ifdef LN_DEBUG
    fprintf(fp,", set col %zu", 1+((plen+l->pos) % l->cols));
#endif
    if (cursorSetColumn(l, ((prompt->charlen+l->pos) % l->cols)) == -1) return -1;

    l->oldpos = l->pos;
    l->oldrpos = rpos2;

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

/* Reset the display state when moved to new line. */
void resetOnNewline(struct linenoiseState *l)
{
    l->maxrows = 0;
    l->is_displayed = false;
    l->needs_refresh = true;
}

/* Prepare output for custom printing with no prompt; undo the raw mode.
 * Returns 0 on success, -1 on error. */
static int prepareCustomOutputClearLine(struct linenoiseState *l)
{
    if (l->is_displayed) {
        struct linenoiseState oldstate = *l;
        l->tempprompt.buf = _T("");
        l->tempprompt.bytelen = l->tempprompt.charlen = 0;
        l->line.charlen = l->line.bytelen = l->pos = 0;

        if (refreshLine(l) == -1) return -1;

        l->tempprompt = oldstate.tempprompt;
        l->tempprompt.bytelen = oldstate.tempprompt.bytelen;
        l->tempprompt.charlen = oldstate.tempprompt.charlen;
        l->pos = oldstate.pos;
        l->line.charlen = oldstate.line.charlen;
        l->line.bytelen = oldstate.line.bytelen;

        resetOnNewline(l);
    }
    return 0;
}

/* Prepare output for custom printing with no prompt; undo the raw mode. */
int linenoiseCustomOutput()
{
    int result = 0;
    int saved_errno = errno;

    if (ensureInitialized(&state) == -1) return -1;
    if (!state.is_supported) return 0;

    result = prepareCustomOutputClearLine(&state);
    disableRawMode(&state);
    errno = saved_errno;
    return result;
}

/* Prepare custom output on new line.
 * Returns 0 on success, -1 on error. */
static int prepareCustomOutputOnNewLine(struct linenoiseState *l)
{
    if (l->is_displayed) {
        struct linenoiseState oldstate = *l;
        l->pos = l->line.charlen;
        if (refreshLine(l) == -1) return -1;

        _tprintf(_T("\r\n"));

        l->pos = oldstate.pos;

        resetOnNewline(l);
    }
    return 0;
}

/* Ensure that the read buffer is big enough.
 * Returns 0 on success, -1 on error. */
static int ensureBufLen(struct linenoiseString *s, size_t requestedBufLen)
{
    size_t newlen = s->buflen;
    if (s->buflen == 0) {
        newlen = requestedBufLen;
    } else if (s->buflen < requestedBufLen) {
        newlen = s->buflen + LINENOISE_LINE_INIT_MAX_AND_GROW;
        while (newlen < requestedBufLen)
            newlen += LINENOISE_LINE_INIT_MAX_AND_GROW;
    } else
        return 0;

    if (s->buflen < newlen) {
        char_t *newbuf = s->buf ? realloc(s->buf, newlen*sizeof(char_t)) :
                                  malloc(newlen*sizeof(char_t));
        charpos_t *newcharindex = s->charindex ? realloc(s->charindex, newlen*sizeof(charpos_t)) :
                                                 malloc(newlen*sizeof(charpos_t));
        s->buf = newbuf != NULL ? newbuf : s->buf;
        s->charindex = newcharindex != NULL ? newcharindex : s->charindex;
        if (newbuf == NULL || newcharindex == NULL) {
            errno = ENOMEM;
            return -1;
        }
        s->buf = newbuf;
        s->charindex = newcharindex;

        if (s->buflen == 0)
            s->buf[0] = '\0';

        s->buflen = newlen;
        s->buf[s->buflen-1] = '\0';
        s->charindex[0] = 0;
    }
    return 0;
}

#ifndef _WIN32
/* Decode ANSI escape sequence.
 * Returns ReadCharSpecials or RCS_NONE in case unknown sequence is read. */
const struct linenoiseChar *ansiDecode(struct linenoiseAnsi *la)
{
    if (la->ansi_sequence_meaning == ASM_CSI) {
        switch (la->ansi_final)
        {
        case 0x41:
            if (la->ansi_parameter_len == 0)
                return getChar(&la->temp_char, RCS_CURSOR_UP);
            else
                break;
        case 0x42:
            if (la->ansi_parameter_len == 0)
                return getChar(&la->temp_char, RCS_CURSOR_DOWN);
            else
                break;
        case 0x43:
            if (la->ansi_parameter_len == 0)
                return getChar(&la->temp_char, RCS_CURSOR_RIGHT);
            else
                break;
        case 0x44:
            if (la->ansi_parameter_len == 0)
                return getChar(&la->temp_char, RCS_CURSOR_LEFT);
            else
                break;
        case 0x46:
            if (la->ansi_parameter_len == 0)
                return getChar(&la->temp_char, RCS_END);
            else
                break;
        case 0x48:
            if (la->ansi_parameter_len == 0)
                return getChar(&la->temp_char, RCS_HOME);
            else
                break;
        case 0x7E: {    /* 0x70 to 0x7E are fpr private use as per ECMA-048 */
            if (strcmp(la->ansi_parameter, "1") == 0)
                return getChar(&la->temp_char, RCS_HOME);
            else if (strcmp(la->ansi_parameter, "3") == 0)
                return getChar(&la->temp_char, RCS_DELETE);
            if (strcmp(la->ansi_parameter, "4") == 0)
                return getChar(&la->temp_char, RCS_END);
            break;
        }
        default: break;
        }
    }
    return &CHAR_NONE;
}

/* Add ANSI character sequence and process the state.
 * Return true in case of success, false on failure. */
bool ansiAddCharacter(struct linenoiseAnsi *la, const struct linenoiseChar *c)
{
    /* Ignore DEL */
    if (c->unicodeChar == CERASE)
        return true;

    if (la->ansi_escape_len == 0) {
        la->ansi_escape[la->ansi_escape_len++] = *c;
        la->ansi_intermediate_len = 0;
        la->ansi_parameter_len = 0;
        la->ansi_sequence_meaning = ASM_C1;
        la->ansi_state = AES_INTERMEDIATE;
    } else {
        if (la->ansi_state == AES_INTERMEDIATE && c->unicodeChar >= 0x20 && c->unicodeChar <= 0x2F) {
            la->ansi_escape[la->ansi_escape_len++] = *c;
            la->ansi_intermediate[la->ansi_intermediate_len++] = (char_t)c->unicodeChar;
        } else if (la->ansi_state == AES_CSI_INTERMEDIATE && c->unicodeChar >= 0x20 && c->unicodeChar < 0x2F) {
            la->ansi_escape[la->ansi_escape_len++] = *c;
            la->ansi_intermediate[la->ansi_intermediate_len++] = (char_t)c->unicodeChar;
        } else if (la->ansi_state == AES_CSI_PARAMETER && c->unicodeChar >= 0x20 && c->unicodeChar < 0x2F) {
            la->ansi_state = AES_CSI_INTERMEDIATE;
            la->ansi_escape[la->ansi_escape_len++] = *c;
            la->ansi_intermediate[la->ansi_intermediate_len++] = (char_t)c->unicodeChar;
        } else if (la->ansi_state == AES_CSI_PARAMETER && c->unicodeChar >= 0x30 && c->unicodeChar < 0x3F) {
            la->ansi_escape[la->ansi_escape_len++] = *c;
            la->ansi_parameter[la->ansi_parameter_len++] = (char_t)c->unicodeChar;
        } else if (la->ansi_state == AES_INTERMEDIATE && c->unicodeChar >= 0x30 && c->unicodeChar < 0x7F ) {
            la->ansi_escape[la->ansi_escape_len++] = *c;
            if (la->ansi_escape_len == 2 && c->unicodeChar == 0x5B) {
                la->ansi_state = AES_CSI_PARAMETER;
                la->ansi_sequence_meaning = ASM_CSI;
            } else if (la->ansi_escape_len == 2 && c->unicodeChar == 0x4E) {
                la->ansi_state = AES_SS_CHARACTER;
                la->ansi_sequence_meaning = ASM_G2;
            } else if (la->ansi_escape_len == 2 && c->unicodeChar == 0x4F) {
                la->ansi_state = AES_SS_CHARACTER;
                la->ansi_sequence_meaning = ASM_G3;
            } else {
                la->ansi_final = (char_t)c->unicodeChar;
                la->ansi_state = AES_FINAL;
            }
        } else if ((la->ansi_state == AES_CSI_INTERMEDIATE || la->ansi_state == AES_CSI_PARAMETER) && c->unicodeChar >= 0x40 && c->unicodeChar < 0x7F) {
            la->ansi_escape[la->ansi_escape_len++] = *c;
            la->ansi_final = (char_t)c->unicodeChar;
            la->ansi_state = AES_FINAL;
        } else if (la->ansi_state == AES_SS_CHARACTER) {
            la->ansi_escape[la->ansi_escape_len++] = *c;
            la->ansi_final = (char_t)c->unicodeChar;
            la->ansi_state = AES_FINAL;
        } else {
            /* Invalid character */
            return false;
        }
    }
    if (la->ansi_state == AES_FINAL) {
        la->ansi_escape[la->ansi_escape_len] = CHAR_NONE;
        la->ansi_parameter[la->ansi_parameter_len] = '\0';
        la->ansi_intermediate[la->ansi_intermediate_len] = '\0';
    }
    return la->ansi_escape_len != ANSI_ESCAPE_MAX_LEN || la->ansi_state == AES_FINAL;
}
#endif

/* Add character or ReadCharSpecials value to be first returned by next call to
 * readChar(). Returns true if successful, false if argument was RCS_NONE. */
bool pushFrontChar(struct linenoiseState *l, const linenoiseChar *c)
{
    if (c->unicodeChar != RCS_NONE) {
        if (l->read_back_char_len == READ_BACK_MAX_LEN)
            l->read_back_char_len--;
        if (l->read_back_char_len > 0)
            memmove(l->read_back_char + 1, l->read_back_char,
                    l->read_back_char_len * sizeof(linenoiseChar));
        l->read_back_char[0] = *c;
        l->read_back_char_len++;
        return true;
    } else {
        return false;
    }
}

/* Push back string of 'c' characters to be read. */
bool pushBackChars(struct linenoiseState *l, const struct linenoiseChar *c, int count)
{
    int i;
    const struct linenoiseChar *pc = c;
    bool allAdded = false;
    if (count > 0) {
        allAdded = true;
        for (i = 0; i < count; i++) {
            if (pc->unicodeChar != RCS_NONE && l->read_back_char_len < READ_BACK_MAX_LEN) {
                l->read_back_char[l->read_back_char_len++] = *pc;
            } else {
                allAdded = false;
                if (l->read_back_char_len == READ_BACK_MAX_LEN)
                    break;
            }
            ++pc;
        }
    }
    return allAdded;
}

/* Push back all characters from 'c' separately to be read. */
bool pushBackRawChars(struct linenoiseState *l, const struct linenoiseChar *c)
{
    if (c->rawCharsLen > 0) {
        size_t i;
        bool allAdded = true;
        for (i = 0; i < c->rawCharsLen; i++) {
            struct linenoiseChar character;
            (void) getChar(&character, c->rawChars[i]);
            allAdded = pushBackChars(l, &character, 1) && allAdded;
        }
        return allAdded;
    } else
        return false;
}

/* Read single character.
 * Returns character code, or one of ReadCharSpecials values, but never
 * RCS_NONE (0). */
const linenoiseChar *readRawChar(struct linenoiseState *l)
{
    static const linenoiseChar NONE = { RCS_NONE, {0}, 0 };

    const linenoiseChar *result = NULL;
    int nread = -1;
    int selectresult = 1;
#ifdef _WIN32
    bool keep_going = true;
#else
    uchar_t c;
#endif

    if (l->rawChar.is_emited) {
        l->rawChar.currentChar.unicodeChar = RCS_NONE;
        l->rawChar.currentChar.rawCharsLen = 0;
        l->rawChar.is_emited = false;
        memset(&l->rawChar.readingState, 0, sizeof(mbstate_t));
    } else if (l->rawChar.currentChar.unicodeChar != RCS_NONE) {
        l->rawChar.is_emited = true;
        return &l->rawChar.currentChar;
    }

#ifdef _WIN32
    while (keep_going) {
        DWORD numEvents = 0;
        {

            HANDLE handles[] = {l->fdin, l->wakeup_event};
            DWORD result;
            (void) revertSignals(l);
            result = WaitForMultipleObjects(2, handles, FALSE, l->is_async ? 0 : INFINITE);
            (void) blockSignals(l);
            if (result == WAIT_OBJECT_0) {
                GetNumberOfConsoleInputEvents(l->fdin, &numEvents);
                assert(numEvents > 0);
            } else if (result == WAIT_OBJECT_0 + 1) {
                errno = EINTR;
                nread = -1;
                keep_going = false;
            } else {
                errno = EAGAIN;
                nread = -1;
                keep_going = false;
            }
        }
        if (numEvents > 0) {
            while (keep_going && numEvents > 0) {
                INPUT_RECORD record;
                DWORD numRead = 0;
                ReadConsoleInput(l->fdin, &record, 1, &numRead);
                if (numRead > 0) {
                    numEvents -= numRead;
                    switch (record.EventType)
                    {
                    case WINDOW_BUFFER_SIZE_EVENT:
                        errno = EINTR;
                        nread = -1;
                        keep_going = false;
                        updateSize();
                        break;
                    case KEY_EVENT:
                        {
                            int countRead = 0;
                            WORD scanCode = record.Event.KeyEvent.wVirtualScanCode;
                            if (!record.Event.KeyEvent.bKeyDown) {
                                continue;
                            }
    #ifdef _UNICODE
                            if (record.Event.KeyEvent.uChar.UnicodeChar != 0) {
                                getChar(&l->rawChar.currentChar, record.Event.KeyEvent.uChar.UnicodeChar);
                            }
    #else
                            if (record.Event.KeyEvent.uChar.AsciiChar != 0) {
                                wchar_t wide[2] = { 0, 0 };
                                wchar_t *start = wide;
                                wchar_t *end = wide + 2;
                                if (MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS,
                                        &record.Event.KeyEvent.uChar.AsciiChar, 1, (LPWSTR)&wide, 2) != 0) {
                                    l->rawChar.currentChar.unicodeChar = fromUtf16(wide[0], wide[1]);
                                    if (l->rawChar.currentChar.unicodeChar != 0) {
                                        l->rawChar.currentChar.rawChars[0] = record.Event.KeyEvent.uChar.AsciiChar;
                                        l->rawChar.currentChar.rawCharsLen = 1;
                                    }
                                }
                            }
    #endif
                            if (l->rawChar.currentChar.unicodeChar != 0) {
                                nread = 1;
                                keep_going = false;
                            }
                            if (keep_going) {
                                unicode_t foundChar = RCS_NONE;
                                switch (record.Event.KeyEvent.wVirtualKeyCode)
                                {
                                case VK_LEFT: foundChar = RCS_CURSOR_LEFT; break;
                                case VK_RIGHT: foundChar = RCS_CURSOR_RIGHT; break;
                                case VK_UP: foundChar = RCS_CURSOR_UP; break;
                                case VK_DOWN: foundChar = RCS_CURSOR_DOWN; break;
                                case VK_HOME: foundChar = RCS_HOME; break;
                                case VK_END: foundChar = RCS_END; break;
                                case VK_DELETE: foundChar = RCS_DELETE; break;
                                default: break;
                                }
                                if (foundChar != RCS_NONE) {
                                    getChar(&l->rawChar.currentChar, foundChar);
                                    nread = 1;
                                    keep_going = false;
                                }
                            }
                            break;
                        }
                    default:
                        break;
                    }
                } else {
                    numEvents = 0;
                }
            }
        }
    }
#else
    /* pselect is always interrupted by EINTR in case of an interrupt */
    if (l->is_async) {
        /* Temporarily restore old mask */
        (void) revertSignals(l);
        nread = read(l->fd, &c, 1);
        (void) blockSignals(l);
    } else {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(l->fd, &fds);

        /* Do select with enabled interrupts */
        selectresult = pselect(l->fd + 1, &fds, NULL, NULL, NULL, &l->sigint_oldmask);
        if (selectresult == 1)
            nread = read(l->fd, &c, 1);
    }

    if (nread > 0) {
        int saved_errno = errno;
        size_t insertIndex = l->rawChar.currentChar.rawCharsLen;
        size_t validChars;

        l->rawChar.currentChar.rawCharsLen++;
        l->rawChar.currentChar.rawChars[insertIndex] = c;
        validChars = mbrlen(
                (char_t*)l->rawChar.currentChar.rawChars + insertIndex, 1,
                &l->rawChar.readingState);
        errno = saved_errno;
        if (validChars == (size_t)-1) {
            pushBackRawChars(l, &l->rawChar.currentChar);
            l->rawChar.is_emited = true;
        } else if (validChars != (size_t)-2) {
            int saved_errno = errno;
            wchar_t character = 0;
            const char_t *rawChars = (char_t*)l->rawChar.currentChar.rawChars;
            mbstate_t state;
            memset(&state, 0, sizeof(state));
            l->rawChar.currentChar.rawChars[l->rawChar.currentChar.rawCharsLen] =
                    '\0';
            if (mbsrtowcs(&character, &rawChars, 1, &state) == (size_t)-1) {
                pushBackRawChars(l, &l->rawChar.currentChar);
                l->rawChar.is_emited = true;
            } else {
                l->rawChar.currentChar.unicodeChar = character;
            }
            errno = saved_errno;
        }
        if (l->rawChar.currentChar.rawCharsLen == MAX_RAW_CHARS) {
            /* No more characters to store, but still no valid one read */
            pushBackRawChars(l, &l->rawChar.currentChar);
            l->rawChar.is_emited = true;
        }
    }
#endif

    if (nread == 0) {
        /* End of stream */
        if (l->rawChar.currentChar.rawCharsLen > 0) {
            pushBackRawChars(l, &l->rawChar.currentChar);
            l->rawChar.is_emited = true;
        } else {
            l->rawChar.currentChar.unicodeChar = RCS_CLOSED;
        }
    } else if (nread < 0) {
        /* Some error */
        result = &CHAR_ERROR;
    }

    if (l->is_cancelled) {
        l->is_cancelled = false;
        result = &CHAR_CANCELLED;
    }

    if (result == NULL && !l->rawChar.is_emited && l->rawChar.currentChar.unicodeChar != RCS_NONE) {
        l->rawChar.is_emited = true;
        result = &l->rawChar.currentChar;
    } else if (result == NULL) {
        result = &NONE;
    }

    return result;
}

/* Read character and decode ANSI escape sequences.
 * Returns character code (value greater than zero), or one of ReadCharSpecials
 * values, but never RCS_NONE (0). */
const linenoiseChar *readChar(struct linenoiseState *l)
{
    const linenoiseChar *c = &CHAR_NONE;

    if (l->is_cancelled) {
        l->is_cancelled = false;
        return &CHAR_CANCELLED;
    }

    while (c->unicodeChar == RCS_NONE
            || (c->unicodeChar == RCS_ERROR && errno == EINTR)) {
        int saved_errno;

        if (l->needs_refresh) {
            if (refreshLine(l) == -1) return &CHAR_ERROR;
        }

        if (l->read_back_char_len > 0) {
            l->read_back_return = l->read_back_char[0];
            c = &l->read_back_return;
            l->read_back_char_len--;
            if (l->read_back_char_len > 0) {
                memmove(l->read_back_char, l->read_back_char + 1,
                        l->read_back_char_len * sizeof(linenoiseChar));
            }
            /* Break now - do not allow further processing (prevent looping over */
            /* the same wrong escape sequences) */
            if (c->unicodeChar != RCS_NONE)
                break;
        }

        if (l->cached_read_char.unicodeChar != RCS_NONE) {
            l->read_back_return = l->cached_read_char;
            l->cached_read_char.unicodeChar = RCS_NONE;
            c = &l->read_back_return;
        } else {
            c = readRawChar(l);
        }

        saved_errno = errno;

        if (l->needs_refresh) {
            if (refreshLine(l) == -1) return &CHAR_ERROR;
        }

#ifndef _WIN32
        /* Check expiration of ESC key and sequence recognition */
        if (l->ansi.ansi_timer_is_active) {
            /* Re-evaluate the logic to use the new character */
            struct itimerspec timerExpiry = {{0, 0}, {0, 0}};
            if (timer_gettime(l->ansi.ansi_timer, &timerExpiry) == -1) return &CHAR_ERROR;
            if (timerExpiry.it_value.tv_sec == 0 && timerExpiry.it_value.tv_nsec == 0) {
                /* Timer expired - it was an ESC key */
                l->ansi.ansi_timer_is_active = false;
                if (c->unicodeChar > RCS_NONE) {
                    l->cached_read_char = *c;
                    pushFrontChar(l, &l->ansi.escape);
                    c = &CHAR_NONE;
                } else {
                    pushFrontChar(l, &l->ansi.escape);
                    if (c->unicodeChar == RCS_CLOSED
                        || (c->unicodeChar == RCS_ERROR
                                && (saved_errno == EINTR
                                || saved_errno == EAGAIN
                                || saved_errno == EWOULDBLOCK))) {
                        c = &CHAR_NONE;
                    }
                }
            } else if (c->unicodeChar > RCS_NONE) {
                /* Timer has not expired and we received a key */
                struct itimerspec timerDisarm = {{0, 0}, {0, 0}};
                l->ansi.ansi_timer_is_active = false;
                (void) ansiAddCharacter(&l->ansi, &l->ansi.escape);

                if (timer_settime(l->ansi.ansi_timer, 0, &timerDisarm, NULL) == -1)
                    return &CHAR_ERROR;
            }
        }
#endif
        errno = saved_errno;

#ifndef _WIN32
        if (c->unicodeChar > RCS_NONE) {
            if (l->ansi.ansi_escape_len == 0 && c->unicodeChar == CESC) {
                struct itimerspec timerNext = { { 0, 0 }, { 0,
                        ANSI_ESCAPE_WAIT_MS * 1000000L } };

                /* ANSI escape begin, set the timer */
                l->ansi.escape = *c;

                if (timer_settime(l->ansi.ansi_timer, 0, &timerNext, NULL) == -1) return &CHAR_ERROR;
                l->ansi.ansi_timer_is_active = true;
                c = &CHAR_NONE;
            } else if (l->ansi.ansi_escape_len != 0) {
                /* ANSI escape continuation */
                if (ansiAddCharacter(&l->ansi, c)) {
                    if (l->ansi.ansi_state == AES_FINAL) {
                        c = ansiDecode(&l->ansi);
                        l->ansi.ansi_escape_len = 0;
                    } else {
                        c = &CHAR_NONE;
                    }
                } else {
                    pushBackChars(l, l->ansi.ansi_escape, l->ansi.ansi_escape_len);
                    l->cached_read_char = *c;
                    l->ansi.ansi_escape_len = 0;
                    c = &CHAR_NONE;
                }
            }
        } else if (l->ansi.ansi_escape_len != 0 && c->unicodeChar == RCS_CLOSED) {
            pushBackChars(l, l->ansi.ansi_escape, l->ansi.ansi_escape_len);
            l->ansi.ansi_escape_len = 0;
            c = &CHAR_NONE;
        }
#endif
    }
    return c;
}

/* Insert the character 'c' at position. */
int insertChar(struct linenoiseString *s, const struct linenoiseChar *c, size_t charpos) {
    if (c->unicodeChar >= 32 && c->rawCharsLen > 0) {
        size_t pos = charpos <= s->charlen ? charpos : s->charlen;
        size_t newLen = s->bytelen + c->rawCharsLen;
        if (ensureBufLen(s, newLen + 1) == -1) return -1;
        if (s->charlen == pos) {
            memcpy(s->buf+s->bytelen, c->rawChars, c->rawCharsLen*sizeof(char_t));
            s->charlen++;
            s->bytelen += c->rawCharsLen;
            s->buf[s->bytelen] = '\0';
            s->charindex[s->charlen] = s->bytelen;
        } else {
            size_t i;
            memmove(getCharAt(s, pos)+c->rawCharsLen, getCharAt(s, pos), (s->bytelen-s->charindex[pos]+1)*sizeof(char_t));
            memmove(s->charindex+pos+1, s->charindex+pos, (s->charlen-pos+1)*sizeof(charpos_t));
            memcpy(getCharAt(s, pos), c->rawChars, c->rawCharsLen*sizeof(char_t));
            s->charlen++;
            s->bytelen += c->rawCharsLen;
            for (i = pos+1; i <= s->charlen; ++i)
                s->charindex[i] += c->rawCharsLen;
            s->buf[s->bytelen] = '\0';
        }
        return 1;
    }
    return 0;
}

/* Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. */
int linenoiseEditInsert(struct linenoiseState *l, const struct linenoiseChar *c) {
    int inserted = insertChar(&l->line, c, l->pos);
    if (inserted == -1) return -1;
    if (inserted > 0) {
        l->pos++;
        if (l->pos == l->line.charlen) {
            if ((!mlmode && l->line.charlen+l->line.charlen < l->cols) /* || mlmode */) {
                /* Avoid a full update of the line in the
                 * trivial case. */
                if (writeChar(l, c) == -1) return -1;
            } else {
                if (refreshLine(l) == -1) return -1;
            }
        } else {
            if (refreshLine(l) == -1) return -1;
        }
        return 0;
    } else {
        return 0;
    }
}

/* Replace character at position */
int linenoiseEditReplace(struct linenoiseState *l, const struct linenoiseChar *c) {
    if (c->unicodeChar >= 32 && c->rawCharsLen > 0) {
        if (l->line.charlen == l->pos) {
            return linenoiseEditInsert(l, c);
        } else {
            size_t i;
            size_t curCharLen = getCharSizeAt(&l->line, l->pos);
            int diff;
            if (c->rawCharsLen > curCharLen)
                diff = (int)(c->rawCharsLen > curCharLen);
            else
                diff = -(int)(curCharLen - c->rawCharsLen);
            if (diff > 0) {
                size_t newLen = l->line.bytelen + diff;
                if (ensureBufLen(&l->line, newLen + 1) == -1) return -1;
            }
            memmove(getCharAt(&l->line, l->pos)+c->rawCharsLen, getCharAt(&l->line, l->pos)+curCharLen,
                    (l->line.charindex[l->line.charlen] - l->line.charindex[l->pos + 1])*sizeof(char_t));
            memcpy(getCharAt(&l->line, l->pos), c->rawChars, c->rawCharsLen*sizeof(char_t));
            l->pos++;
            l->line.bytelen += diff;
            for (i = l->pos; i <= l->line.charlen; ++i)
                l->line.charindex[i] += diff;
            l->line.buf[l->line.bytelen] = '\0';
            if (refreshLine(l) == -1) return -1;
            return 0;
        }
    } else {
        return 0;
    }
}

/* Cancel the line editing. */
int cancelInternal(struct linenoiseState *l)
{
    linenoiseChar c;
    if (linenoiseEditReplace(l, getChar(&c, '^')) == -1) return -1;
    if (linenoiseEditReplace(l, getChar(&c, 'C')) == -1) return -1;

    if (mlmode) {
        l->pos = l->line.charlen;
        if (refreshLine(l) == -1) return -1;
    }

    clearString(&l->line);
    resetOnNewline(l);

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
    if (l->pos != l->line.charlen) {
        l->pos++;
        return refreshLine(l);
    }
    else
        return 0;
}

/* Update current history entry from the current buffer. */
int linenoiseEditUpdateHistoryEntry(struct linenoiseState *l) {
    if (history_len > 1) {
        free(history[history_len - 1 - l->history_index]);
        history[history_len - 1 - l->history_index] = _tcsdup(l->line.buf);
        if (history[history_len - 1 - l->history_index] == NULL) {
            errno = ENOMEM;
            return -1;
        }
    }
    return 0;
}

/* Show history entry. */
int linenoiseShowHistoryEntry(struct linenoiseState *l, int index, size_t pos) {
    size_t hist_strlen;
    l->history_index = index;
    if (l->history_index < 0) {
        if (linenoiseBeep() == -1) return -1;
        l->history_index = 0;
        return 0;
    } else if (l->history_index >= history_len) {
        if (linenoiseBeep() == -1) return -1;
        l->history_index = history_len - 1;
        return 0;
    }
    hist_strlen = _tcslen(history[history_len - 1 - l->history_index]);
    replaceLine(l, history[history_len - 1 - l->history_index], hist_strlen);
    l->pos = MIN(l->line.charlen, pos);
    return refreshLine(l);

}

/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1
int linenoiseEditHistoryNext(struct linenoiseState *l, int dir) {
    if (history_len > 1) {
        int new_index;

        /* Update the current history entry before to
         * overwrite it with the next one. */
        if (linenoiseEditUpdateHistoryEntry(l) == -1) return -1;

        /* Show the new entry */
        new_index = l->history_index + ((dir == LINENOISE_HISTORY_PREV) ? 1 : -1);
        return linenoiseShowHistoryEntry(l, new_index, SIZE_MAX);
    } else {
        if (linenoiseBeep() == -1) return -1;
        return 0;
    }
}

/* Delete character at position. */
int deleteChar(struct linenoiseString *s, size_t pos)
{
    if (s->charlen > 0 && pos < s->charlen) {
        size_t i;
        size_t charSize = getCharSizeAt(s, pos);
        memmove(getCharAt(s, pos), getCharAt(s, pos + 1),
                (s->bytelen - s->charindex[pos + 1] + 1) * sizeof(char_t));
        memmove(s->charindex + pos, s->charindex + pos + 1,
                (s->charlen-(pos+1)+1) * sizeof(charpos_t));
        s->charlen--;
        s->bytelen -= charSize;
        for (i = pos; i <= s->charlen; i++)
            s->charindex[i] -= charSize;
        return 1;
    }
    return 0;
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
int linenoiseEditDelete(struct linenoiseState *l) {
    if (deleteChar(&l->line, l->pos) > 0) {
        return refreshLine(l);
    } else return 0;
}

/* Backspace implementation. */
int linenoiseEditBackspace(struct linenoiseState *l) {
    if (l->pos > 0 && l->line.charlen > 0) {
        l->pos--;
        return linenoiseEditDelete(l);
    } else return 0;
}

/* Delete the previosu word, maintaining the cursor at the start of the
 * current word. */
int linenoiseEditDeletePrevWord(struct linenoiseState *l) {
    size_t old_pos = l->pos;
    size_t chardiff;
    size_t bytediff;
    size_t i;

    while (l->pos > 0 && getUnicodeCharAt(&l->line, l->pos-1) == ' ')
        l->pos--;
    while (l->pos > 0 && getUnicodeCharAt(&l->line, l->pos-1) != ' ')
        l->pos--;
    chardiff = old_pos - l->pos;
    bytediff = l->line.charindex[old_pos] - l->line.charindex[l->pos];
    memmove(getCharAt(&l->line, l->pos), getCharAt(&l->line, old_pos),
            (l->line.bytelen - l->line.charindex[old_pos] + 1)*sizeof(char_t));
    memmove(l->line.charindex+l->pos, l->line.charindex+old_pos,
            (l->line.charlen-old_pos+1)*sizeof(charpos_t));
    l->line.charlen -= chardiff;
    l->line.bytelen -= bytediff;
    for (i = l->pos; i <= l->line.charlen; i++)
        l->line.charindex[i] -= bytediff;
    return refreshLine(l);
}

/* Swap single character with previous one. */
int linenoiseEditSwapCharWithPrevious(struct linenoiseState *l) {
    if (l->pos >= 2 && l->pos == l->line.charlen)
        l->pos--;
    if (l->pos > 0 && l->pos < l->line.charlen) {
        char_t prev[MAX_RAW_CHARS];
        size_t prevSize = getCharSizeAt(&l->line, l->pos-1);
        size_t curSize = getCharSizeAt(&l->line, l->pos);
        memcpy(prev, getCharAt(&l->line, l->pos-1), prevSize*sizeof(char_t));
        memmove(getCharAt(&l->line, l->pos-1), getCharAt(&l->line, l->pos), curSize*sizeof(char_t));
        memcpy(getCharAt(&l->line, l->pos-1)+curSize, prev, prevSize*sizeof(char_t));
        l->line.charindex[l->pos] = l->line.charindex[l->pos-1] + curSize;
        l->line.charindex[l->pos+1] = l->line.charindex[l->pos] + prevSize;
        if (l->pos != l->line.charlen) l->pos++;
        if (refreshLine(l) == -1) return -1;
    }
    return 0;
}

/* Reset state to readling new line. */
int resetState(struct linenoiseState *l)
{
    if (l->state != LS_NEW_LINE) {
        if (l->state == LS_COMPLETION) {
            freeCompletions(l);
        } else if (l->state == LS_HISTORY_SEARCH) {
            if (freeHistorySearch(l) == -1) return -1;
        }

        history_len--;
        free(history[history_len]);

        l->state = LS_NEW_LINE;
    }
    return 0;
}

/* Set closed state. */
int setClosed(struct linenoiseState *l)
{
    l->is_closed = true;
    return resetState(l);
}

/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is updated in 'line.buf' and editing ends, when
 * the user types enter, or when ctrl+d is typed on an empty line. Line
 * editing can be cancelled by pressing ctrl+c, in which case LR_CANCELLED
 * is returned.
 *
 * The function returns one of the LinenoiseResult values to indicate the
 * line editing result. */
static enum LinenoiseResult linenoiseEdit(struct linenoiseState *l)
{
    const linenoiseChar *c = NULL;

    /* The latest history entry is always our current buffer, that
     * initially is just an empty string. */
    if (l->state == LS_NEW_LINE) {
        /* Buffer starts empty. */
        if (l->pos != 0 || l->line.charlen != 0) {
            clearString(&l->line);
            l->pos = 0;
            l->needs_refresh = true;
        }

        linenoiseHistoryAdd(_T(""));
        l->history_index = 0;
        l->state = LS_READ;
    }

    if (l->needs_refresh || !l->is_displayed)
        if (refreshLine(l) == -1) return LR_ERROR;

    c = readChar(l);

    if (c->unicodeChar == RCS_CLOSED) {
        if (setClosed(l) == -1) return LR_ERROR;
        return LR_CLOSED;
    }
    else if (c->unicodeChar == RCS_ERROR) {
        return LR_ERROR;
    }

    switch(c->unicodeChar) {
    case CTRL('M'):     /* enter */
        if (resetState(l) == -1) return LR_ERROR;
        return LR_HAVE_TEXT;
    case RCS_CANCELLED:
    case CTRL('C'):     /* ctrl-c */
    {
        bool doCancel = (l->line.charlen == 0);
        if (cancelInternal(l) == -1) return LR_ERROR;
        if (resetState(l) == -1) return LR_ERROR;
        if (doCancel)
            return LR_CANCELLED;
        else {
            if (_tprintf(_T("\r\n")) < 0) return LR_ERROR;
            return LR_CONTINUE;
        }
    }
    case CERASE:           /* backspace */
    case CTRL('H'):     /* ctrl-h */
        if (linenoiseEditBackspace(l) == -1) return LR_ERROR;
        break;
    case CTRL('D'):     /* ctrl-d, remove char at right of cursor, or of the
                   line is empty, act as end-of-file. */
        if (l->line.charlen > 0) {
            if (linenoiseEditDelete(l) == -1) return LR_ERROR;
        }
#ifndef _WIN32
        else {
            if (setClosed(l) == -1) return LR_ERROR;
            return LR_CLOSED;
        }
#endif
        break;
#ifdef _WIN32
    case CTRL('Z'):
        if (setClosed(l) == -1) return LR_ERROR;
        return LR_CLOSED;
        break;
#endif
    case CTRL('T'):     /* ctrl-t, swaps current character with previous. */
        if (linenoiseEditSwapCharWithPrevious(l) == -1) return LR_ERROR;
        break;
    case CTRL('B'):     /* ctrl-b */
    case RCS_CURSOR_LEFT:
        if (linenoiseEditMoveLeft(l) == -1) return LR_ERROR;
        break;
    case CTRL('F'):     /* ctrl-f */
    case RCS_CURSOR_RIGHT:
        if (linenoiseEditMoveRight(l) == -1) return LR_ERROR;
        break;
    case CTRL('P'):     /* ctrl-p */
    case RCS_CURSOR_UP:
        if (linenoiseEditHistoryNext(l, LINENOISE_HISTORY_PREV) == -1) return LR_ERROR;
        break;
    case CTRL('N'):     /* ctrl-n */
    case RCS_CURSOR_DOWN:
        if (linenoiseEditHistoryNext(l, LINENOISE_HISTORY_NEXT) == -1) return LR_ERROR;
        break;
    case RCS_DELETE:
        if (linenoiseEditDelete(l) == -1) return LR_ERROR;
        break;
    default:
        if (linenoiseEditInsert(l,c) == -1) return LR_ERROR;
        break;
    case CTRL('U'):     /* Ctrl+u, delete the whole line. */
        clearString(&l->line);
        l->pos = 0;
        if (refreshLine(l) == -1) return LR_ERROR;
        break;
    case CTRL('K'):     /* Ctrl+k, delete from current to end of line. */
        l->line.charlen = l->pos;
        l->line.bytelen = l->line.charindex[l->pos];
        l->line.buf[l->line.bytelen] = '\0';
        if (refreshLine(l) == -1) return LR_ERROR;
        break;
    case CTRL('A'):     /* Ctrl+a, go to the start of the line */
    case RCS_HOME:
        l->pos = 0;
        if (refreshLine(l) == -1) return LR_ERROR;
        break;
    case CTRL('E'):     /* ctrl+e, go to the end of the line */
    case RCS_END:
        l->pos = l->line.charlen;
        if (refreshLine(l) == -1) return LR_ERROR;
        break;
    case CTRL('L'):     /* ctrl+l,     clear screen */
        if (clearScreen(l) == -1) return LR_ERROR;
        if (refreshLine(l) == -1) return LR_ERROR;
        break;
    case CTRL('W'):     /* ctrl+w, delete previous word */
        if (linenoiseEditDeletePrevWord(l) == -1) return LR_ERROR;
        break;
    case CTRL('I'):     /* tab, complete */
        /* Only autocomplete when the callback is set. It returns < 0 when
         * there was an error reading from fd. Otherwise it will return the
         * character that should be handled next. */
        if (completionCallback != NULL) {
            pushFrontChar(l, c);
            l->state = LS_COMPLETION;
            return LR_CONTINUE;
        } else {
            if (linenoiseEditInsert(l,c) == -1) return LR_ERROR;
        }
        break;
    case CTRL('R'):     /* ctrl+r, search the history */
        if (linenoiseEditUpdateHistoryEntry(l) == -1) return -1;
        pushFrontChar(l, c);
        l->state = LS_HISTORY_SEARCH;
        return LR_CONTINUE;
    }

    return LR_CONTINUE;
}

/* Handle TAB completion.
 * Returns one of the LinenoiseResult values to indicate the completion result. */
static enum LinenoiseResult linenoiseCompletion(struct linenoiseState *l) {
    const linenoiseChar *c = readChar(l);

    switch (c->unicodeChar) {
    case CTRL('C'):
    case RCS_CANCELLED:
    case RCS_CLOSED:
    default:
        /* Let the normal processing to do its job */
        freeCompletions(l);
        pushFrontChar(l, c);
        l->state = LS_READ;
        return LR_CONTINUE;
    case RCS_ERROR:
        return LR_ERROR;
    case CTRL('I'):
        {
            bool wasInitialized = l->comp.is_initialized;
            if (!wasInitialized || l->comp.len == 1) {
                wasInitialized = false;
                freeCompletions(l);

                completionCallback(l->line.buf, l->pos, &l->comp);

                /* completion might call linenoiseCustomOutput() */
                if (enableRawMode(l) == -1) return LR_ERROR;

                if (l->comp.len > 0 && l->comp.cvec == NULL) {
                    errno = ENOMEM;
                    return LR_ERROR;
                }
                l->comp.is_initialized = true;
            }

            if (wasInitialized || l->comp.len < 2)
                completeLine(l);
            return LR_CONTINUE;
        }
    }
}

/* Free a list of completion option populated by linenoiseAddCompletion(). */
static int freeHistorySearch(struct linenoiseState *l) {
    if (l->hist_search.text.buf != NULL) {
        freeString(&l->hist_search.text);
        l->hist_search.current_index = 0;
        l->hist_search.found = false;
        if (setTempPrompt(l, NULL) == -1) return -1;
    }
    return 0;
}

/* Sets the temporary history searching prompt. */
int setSearchPrompt(struct linenoiseState *l)
{
    int result = 0;
    size_t promptlen = l->hist_search.text.bytelen + 22 + 1;
    char_t* newprompt = calloc(sizeof(char_t), promptlen);
    if (newprompt == NULL) {
        errno = ENOMEM;
        return -1;
    }
    if (l->hist_search.text.buf != NULL)
#ifdef _WIN32
        _sntprintf_s(newprompt, promptlen, _TRUNCATE, _T("(reverse-i-search`%s'): "), l->hist_search.text.buf);
#else
        snprintf(newprompt, promptlen, _T("(reverse-i-search`%s'): "), l->hist_search.text.buf);
#endif
    newprompt[promptlen-1] = _T('\0');
    result = setTempPrompt(l, newprompt);
    free(newprompt);
    return result;
}

/* Find entry in history matching the current sequence */
int linenoiseHistoryFindEntry(struct linenoiseState *l)
{
    if (l->hist_search.text.bytelen > 0) {
        linenoiseString parsed = {NULL, NULL, 0, 0, 0};
        int new_index = l->hist_search.current_index;
        while (new_index < history_len) {
            char_t *historyStr = history[history_len - 1 - new_index];
            char_t *found = _tcsstr(historyStr, l->hist_search.text.buf);
            char_t *last_found = NULL;
            if (found != NULL) {
                size_t historyStrLen = _tcslen(historyStr);
                if (parseLine(historyStr, historyStrLen, &parsed) == -1) return -1;
                found = _tcsstr(parsed.buf, l->hist_search.text.buf);
            }
            while (found != NULL) {
                size_t first = found-parsed.buf;
                size_t last = first + l->hist_search.text.bytelen;
                if (isAtCharBoundary(first, &parsed)
                        && isAtCharBoundary(last, &parsed)) {
                    last_found = found;
                }
                found = _tcsstr(found + 1, l->hist_search.text.buf);
            }
            if (last_found != NULL) {
                if (setSearchPrompt(l) == -1) {
                    freeString(&parsed);
                    return -1;
                }
                l->hist_search.found = true;

                if (ensureBufLen(&l->line, parsed.bytelen+1) == -1) {
                    freeString(&parsed);
                    return -1;
                }
                memcpy(l->line.buf, parsed.buf, (parsed.bytelen+1)*sizeof(char_t));
                memcpy(l->line.charindex, parsed.charindex, (parsed.charlen+1)*sizeof(charpos_t));
                l->line.bytelen = parsed.bytelen;
                l->line.charlen = parsed.charlen;
                l->pos = MIN(l->line.charlen,
                             findNearestCharIndex(last_found - parsed.buf + l->hist_search.text.bytelen,
                                                  &l->line));
                freeString(&parsed);

                l->history_index = new_index;
                if (refreshLine(l) == -1) return -1;
                l->hist_search.current_index = l->history_index;
                return 0;
            }
            new_index++;
        }
        freeString(&parsed);
        linenoiseBeep();
    }
    l->hist_search.found = false;
    return 0;
}

/* Handle the CTRL+R history searching.
 * Returns one of the LinenoiseResult values to indicate the searching result. */
static enum LinenoiseResult linenoiseHistorySearch(struct linenoiseState *l)
{
    const linenoiseChar *c = readChar(l);

    switch (c->unicodeChar) {
    case CTRL('C'):
    case RCS_CANCELLED:
        if (cancelInternal(l) == -1) return LR_ERROR;
        if (_tprintf(_T("\r\n")) < 0) return LR_ERROR;
        if (resetState(l) == -1) return LR_ERROR;
        return LR_CONTINUE;
    case RCS_CLOSED:
        /* Let the normal processing to do its job */
        if (freeHistorySearch(l) == -1) return LR_ERROR;
        pushFrontChar(l, c);
        l->state = LS_READ;
        return LR_CONTINUE;
    case RCS_ERROR:
        return LR_ERROR;
    case CERASE:   /* backspace */
    case CTRL('H'):     /* ctrl-h */
        if (l->hist_search.text.charlen > 0) {
            (void) deleteChar(&l->hist_search.text, l->hist_search.text.charlen-1);
            if (l->hist_search.text.charlen == 0 && l->hist_search.found) {
                if (setSearchPrompt(l) == -1) return LR_ERROR;
                if (refreshLine(l) == -1) return LR_ERROR;
            } else {
                if (linenoiseHistoryFindEntry(l) == -1) return LR_ERROR;
            }
        }
        return LR_CONTINUE;
    default:
        if (c->unicodeChar >= 32) {
            bool wasFound;

            if (insertChar(&l->hist_search.text, c, l->hist_search.text.charlen) == -1) return LR_ERROR;

            /* Find history entry */
            wasFound = l->hist_search.found;
            if (linenoiseHistoryFindEntry(l) == -1) return LR_ERROR;
            if (wasFound && !l->hist_search.found)
                l->hist_search.current_index++;
            return LR_CONTINUE;
        } else {
            /* Cancel */
            if (freeHistorySearch(l) == -1) return LR_ERROR;
            pushFrontChar(l, c);
            l->state = LS_READ;
            return LR_CONTINUE;
        }
    case CTRL('R'):
        if (l->hist_search.text.buf == NULL) {
            /* First search */
            if (history_len == 1) {
                linenoiseBeep();
                l->state = LS_READ;
                return LR_CONTINUE;
            }
            l->hist_search.current_index = l->history_index;
            if (ensureBufLen(&l->hist_search.text, LINENOISE_LINE_INIT_MAX_AND_GROW) == -1)
                return LR_ERROR;
            if (setSearchPrompt(l) == -1) return LR_ERROR;
            if (refreshLine(l) == -1) return LR_ERROR;
            return LR_CONTINUE;
        } else if (l->hist_search.text.charlen > 0) {
            /* Find another history entry */
            if (l->hist_search.found) {
                l->hist_search.current_index++;
                if (linenoiseHistoryFindEntry(l) == -1) return LR_ERROR;
            } else {
                linenoiseBeep();
            }
            return LR_CONTINUE;
        } else {
            /* Do nothing - no string to search for */
            return LR_CONTINUE;
        }
    }
}

/* This function calls the line editing function corresponding to the state,
 * it is either linenoiseEdit(), linenoiseCompletion() or
 * linenoiseHistorySearch(). The terminal is put to raw mode. */
static enum LinenoiseResult linenoiseRaw(struct linenoiseState *l) {
    bool gotBlocked;
    enum LinenoiseResult result = LR_CONTINUE;
    int savederrno;

    if (l->is_closed) {
        return LR_CLOSED;
    } else if (l->line.buflen == 0) {
        errno = EINVAL;
        return LR_ERROR;
    }

    if (enableRawMode(l) == -1) return LR_ERROR;

    gotBlocked = blockSignals(l);

    while (result == LR_CONTINUE) {
        switch (l->state) {
        case LS_NEW_LINE:
        case LS_READ:
        default:
            result = linenoiseEdit(l);
            break;
        case LS_COMPLETION:
            result = linenoiseCompletion(l);
            break;
        case LS_HISTORY_SEARCH:
            result = linenoiseHistorySearch(l);
            break;
        }
    }

    savederrno = errno;

    if (!l->is_async) disableRawMode(l);
    if (gotBlocked) revertSignals(l);

    errno = savederrno;
    return result;
}

/* Ensure the fields are initialized. */
static int ensureInitialized(struct linenoiseState *l)
{
    if (!initialized) {
        blockSignals(l);
        if (!initialized) {
            if (initialize(l) == -1) {
                revertSignals(l);
                return -1;
            }
            initialized = true;
            revertSignals(l);
        }
    } else if (l->is_supported) {
        updateSize();
    }

    if (l->is_supported) {
#ifndef _WIN32
        int flagsRead = fcntl(l->fd, F_GETFL, 0);
        l->is_async = (flagsRead & O_NONBLOCK) != 0;
#endif
    }
    return 0;
}

/* Set the prompt. */
int linenoiseSetPrompt(const char_t *prompt)
{
    if (ensureInitialized(&state) == -1) return -1;
    return setPrompt(&state, prompt);
}

/* Show the prompt. */
int linenoiseShowPrompt()
{
    int result = 0;

    if (ensureInitialized(&state) == -1) return -1;
    if (!state.is_supported) return 0;

    if (enableRawMode(&state) == -1) return -1;
    if (state.needs_refresh || !state.is_displayed)
        result = refreshLine(&state);
    if (result == -1 || !state.is_async)
        disableRawMode(&state);
    return result == 0 ? 1 : result;
}

/* Check if there is anything to process. */
int linenoiseHasPendingChar()
{
    if (ensureInitialized(&state) == -1) return -1;
    if (!state.is_supported) return 0;

    return state.read_back_char_len != 0 || state.is_cancelled;
}

/* Reset from raw mode and go to new line if necessary. */
int linenoiseCleanup()
{
    if (ensureInitialized(&state) == -1) return -1;
    if (!state.is_supported) return 0;

    if (prepareCustomOutputOnNewLine(&state) == -1) return -1;
    disableRawMode(&state);
    return 0;
}

/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
char_t *linenoise() {
    int saved_errno = errno;
    if (ensureInitialized(&state) == -1) return NULL;

    if (!state.is_supported) {
        char_t buf[LINENOISE_LINE_INIT_MAX_AND_GROW];
        size_t len;
        char_t* copy;

        if (state.is_cancelled) {
            errno = EINTR;
            return NULL;
        }

        _tprintf(_T("%s"), state.prompt.buf);
        fflush(stdout);
        if (_fgetts(buf,LINENOISE_LINE_INIT_MAX_AND_GROW,stdin) == NULL) {
            if (state.is_cancelled) {
                errno = EINTR;
            }
            return NULL;
        }

        if (state.is_cancelled) {
            errno = EINTR;
            return NULL;
        }

        len = _tcslen(buf);
        while(len && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            len--;
            buf[len] = '\0';
        }
        copy = _tcsdup(buf);
        if (copy == NULL) errno = ENOMEM;
        return copy;
    } else {
        enum LinenoiseResult result = linenoiseRaw(&state);
        char_t* copy;

        if (result == LR_CANCELLED) {
            resetOnNewline(&state);
            _tprintf(_T("\r\n"));
            if (state.is_async)
                disableRawMode(&state);
            errno = EINTR;
            return NULL;
        } else if (result == LR_CLOSED && state.line.bytelen == 0) {
            resetOnNewline(&state);
            _tprintf(_T("\r\n"));
            errno = 0;
            return NULL;
        } else if (result == LR_ERROR) {
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                resetOnNewline(&state);
                _tprintf(_T("\r\n"));
                if (state.is_async)
                    disableRawMode(&state);
            }
            return NULL;
        } else if (result == LR_HAVE_TEXT) {
            resetOnNewline(&state);
            _tprintf(_T("\r\n"));
        }

        /* Have some text */
        copy = malloc((state.line.bytelen + 1) * sizeof(char_t));
        if (copy != NULL) {
            memcpy(copy, state.line.buf, state.line.bytelen * sizeof(char_t));
            copy[state.line.bytelen] = '\0';
        }

        clearString(&state.line);
        state.pos = 0;

        if (copy == NULL) errno = ENOMEM;
        else errno = saved_errno;

        return copy;
    }
}

/* Cancel the current line editing. */
int linenoiseCancel() {
    if (ensureInitialized(&state) == -1) return -1;
    state.is_cancelled = true;
#ifdef _WIN32
    if (state.is_supported)
        SetEvent(state.wakeup_event);
#endif
    return 0;
}

/* Update the number of columns. */
static void updateSize() {
    if ( setSize(&state) )
    {
        state.needs_refresh = true;
    }
}

#ifndef _WIN32
/* Update the size. */
int linenoiseUpdateSize() {
    if (ensureInitialized(&state) == -1) return -1;
    if (!state.is_supported) return 0;

    updateSize();
    return 0;
}
#endif

void linenoiseSetAsync(int is_async)
{
    state.is_async = is_async != 0;
}

/* Initialize the state */
static int initialize(struct linenoiseState *l)
{
#ifndef _WIN32
    struct sigevent ev = {{0}, SIGALRM, SIGEV_SIGNAL };
#endif

#ifdef _WIN32
    l->fdin = GetStdHandle(STD_INPUT_HANDLE);
    l->fdout = GetStdHandle(STD_OUTPUT_HANDLE);
    l->wakeup_event = CreateEventA(NULL, FALSE, FALSE, NULL);
#else
    l->fd = STDIN_FILENO;
#endif
    l->is_supported = (isUnsupportedTerm(l) == 0);
    if (!l->is_supported) return 0;

    atexit(linenoiseAtExit);

#ifdef _WIN32
    if (ensureBufLen(&state.rawChar.tempString, LINENOISE_TEMP_STRING_SIZE+1) == -1) return -1;
#else
    if (timer_create(CLOCK_MONOTONIC, &ev, &l->ansi.ansi_timer) == -1)
        return -1;
    l->ansi.ansi_timer_created = true;
#endif

    /* Populate the linenoise state that we pass to functions implementing
     * specific editing functionalities. */
    if (ensureBufLen(&l->line, LINENOISE_LINE_INIT_MAX_AND_GROW) == -1) return -1;

    setSize(l);

    return 0;
}

/* Free the state. */
static void freeState()
{
#ifdef _WIN32
    CloseHandle(state.wakeup_event);
    freeString(&state.rawChar.tempString);
#else
    if (state.ansi.ansi_timer_created) {
        timer_delete(state.ansi.ansi_timer);
        state.ansi.ansi_timer_created = false;
    }
#endif
    freeString(&state.prompt);
    freeString(&state.tempprompt);
    freeString(&state.line);
    freeHistorySearch(&state);
    freeCompletions(&state);
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
    disableRawMode(&state);
    freeHistory();
    freeState();
}

/* Using a circular buffer is smarter, but a bit more complex to handle. */
int linenoiseHistoryAdd(const char_t *line) {
    char_t *linecopy;

    if (history_max_len == 0) return 0;
    if (history == NULL) {
        history = malloc(sizeof(char_t*)*history_max_len);
        if (history == NULL) return 0;
        memset(history,0,(sizeof(char_t*)*history_max_len));
    }
    linecopy = _tcsdup(line);
    if (!linecopy) return 0;
    if (history_len == history_max_len) {
        free(history[0]);
        memmove(history,history+1,sizeof(char_t*)*(history_max_len-1));
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
    char_t **new;

    if (len < 1) return 0;
    if (history) {
        int tocopy = history_len;

        new = malloc(sizeof(char_t*)*len);
        if (new == NULL) return 0;

        /* If we can't copy everything, free the elements we'll not use. */
        if (len < tocopy) {
            int j;

            for (j = 0; j < tocopy-len; j++) free(history[j]);
            tocopy = len;
        }
        memset(new,0,sizeof(char_t*)*len);
        memcpy(new,history+(history_len-tocopy), sizeof(char_t*)*tocopy);
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
int linenoiseHistorySave(char_t *filename) {
    FILE *fp;
    int j;

#ifdef _WIN32
#ifdef _UNICODE
    errno = _tfopen_s(&fp, filename, _T("w,ccs=UTF-8"));
#else
    errno = _tfopen_s(&fp, filename, _T("w"));
#endif
#else
    fp = fopen(filename, "w");
#endif
    if (fp == NULL) return -1;
    for (j = 0; j < history_len; j++)
        _ftprintf_s(fp, _T("%s\n"), history[j]);
    fclose(fp);
    return 0;
}

/* Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int linenoiseHistoryLoad(char_t *filename) {
    char_t buf[LINENOISE_LINE_INIT_MAX_AND_GROW];
    FILE *fp;

#ifdef _WIN32
#ifdef _UNICODE
    errno = _tfopen_s(&fp, filename, _T("r,ccs=UTF-8"));
#else
    errno = _tfopen_s(&fp, filename, _T("r"));
#endif
#else
    fp = fopen(filename, "w");
#endif
    
    if (fp == NULL) return -1;

    while (_fgetts(buf,LINENOISE_LINE_INIT_MAX_AND_GROW,fp) != NULL) {
        char_t *p;
        
        p = _tcschr(buf,_T('\r'));
        if (!p) p = _tcschr(buf,_T('\n'));
        if (p) *p = '\0';
        linenoiseHistoryAdd(buf);
    }
    fclose(fp);
    return 0;
}
