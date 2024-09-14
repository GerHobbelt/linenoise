#define SYNTAX_VERSION "0.0.1"

#ifdef __linux__
#define _POSIX_C_SOURCE 200809L
#endif

#ifdef _WIN32 /* Windows platform, either MinGW or Visual Studio (MSVC) */
#include <windows.h>
#include <fcntl.h>
#define USE_WINCONSOLE
#ifdef __MINGW32__
#define HAVE_UNISTD_H
#endif
#else
#include <termios.h>
#include <sys/ioctl.h>
#define USE_TERMIOS
#define HAVE_UNISTD_H
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include "syntax.h"

/* Here we define an array of syntax highlights by extensions, keywords,
 * comments delimiters and flags. */
struct editorSyntax syntax[] = {
    {C_HL_extensions,
     C_HL_keywords,
     "#", ";#", "/*", "*/",
     HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS}};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

erow *row = NULL;
int screencols = 100;

/* ====================== Syntax highlight color scheme  ==================== */
int is_separator(int c)
{
    return c == '\0' || isspace(c) || strchr(",.()+-/*=~%[]{};", c) != NULL;
}

/* Set every byte of row->hl (that corresponds to every character in the line)
 * to the right syntax highlight type (HL_* defines). */
void UpdateSyntax(erow *row)
{
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);

    if (syntax == NULL)
        return; /* No syntax, everything is HL_NORMAL. */

    int i, token_start, in_string, in_comment;
    char *p;
    char **keywords = syntax->keywords;
    char *scs = syntax->singleline_comment_1;
    char *ics = syntax->singleline_comment_2;
    char *mcs = syntax->multiline_comment_start;
    char *mce = syntax->multiline_comment_end;
    int kprefix = 0;
    /* Point to the first non-space char. */
    p = row->render;
    i = 0; /* Current char offset */
    while (*p && isspace(*p))
    {
        p++;
        i++;
    }
    token_start = 1; /* Tell the parser if 'i' points to start of word. */
    in_string = 0;   /* Are we inside "" or '' ? */
    in_comment = 0;  /* Are we inside multi-line comment? */

    while (*p)
    {
        /* Handle # or ;# inline comments. */
        if (token_start && (*p == ics[0] && *(p + 1) == ics[1]) || (*p == scs[0]))
        {
            /* From here to end is a comment */
            memset(row->hl + i, HL_COMMENT, row->size - i);
            return;
        }

        /* Handle "" and '' */
        if (in_string)
        {
            row->hl[i] = HL_STRING;
            if (*p == '\\')
            {
                row->hl[i + 1] = HL_STRING;
                p += 2;
                i += 2;
                token_start = 0;
                continue;
            }
            if (*p == in_string)
                in_string = 0;
            p++;
            i++;
            continue;
        }
        else
        {
            if (*p == '"' || *p == '\'')
            {
                in_string = *p;
                row->hl[i] = HL_STRING;
                p++;
                i++;
                token_start = 0;
                continue;
            }
        }

        /* Handle non printable chars. */
        if (!isprint(*p))
        {
            row->hl[i] = HL_NONPRINT;
            p++;
            i++;
            token_start = 0;
            continue;
        }

        /* Handle numbers */
        if ((isdigit(*p) && (token_start || row->hl[i - 1] == HL_NUMBER)) ||
            (*p == '.' && i > 0 && row->hl[i - 1] == HL_NUMBER))
        {
            row->hl[i] = HL_NUMBER;
            p++;
            i++;
            token_start = 0;
            continue;
        }

        /* Handle keywords and lib calls */
        if (token_start)
        {
            int j;
            for (j = 0; keywords[j]; j++)
            {
                int klen = strlen(keywords[j]);
                kprefix = (keywords[j][klen - 1] == '|');
                if (kprefix)
                    klen--;

                if (!memcmp(p, keywords[j], klen) && (is_separator(*(p + klen)) || kprefix))
                {
                    /* Keyword */
                    memset(row->hl + i, kprefix ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    p += klen;
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL)
            {
                if (!kprefix)
                    token_start = 0;
                continue; /* We had a keyword match */
            }
        }

        token_start = is_separator(*p);
        p++;
        i++;
    }
}

/* ======================= Editor rows implementation ======================= */
#define TAB 9

/* Update the rendered version and the syntax highlight of a row. */
void UpdateRow(erow *row)
{
    unsigned int tabs = 0, nonprint = 0;
    int j, idx;

    /* Create a version of the row we can directly print on the screen,
     * respecting tabs, substituting non printable characters with '?'. */
    free(row->render);
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == TAB)
            tabs++;

    uint64_t allocsize = (uint64_t)row->size;
		allocsize += tabs * 8 + nonprint * 9 + 1;
    if (allocsize > (uint64_t)UINT32_MAX)
    {
        printf("line is too long to be rendered\n");
        exit(1);
    }

    row->render = malloc(row->size + tabs * 8 + nonprint * 9 + 1);
    idx = 0;
    for (j = 0; j < row->size; j++)
    {
        if (row->chars[j] == TAB)
        {
            row->render[idx++] = ' ';
            while ((idx + 1) % 8 != 0)
                row->render[idx++] = ' ';
        }
        else
        {
            row->render[idx++] = row->chars[j];
        }
    }
    row->rsize = idx;
    row->render[idx] = '\0';
}

/* Insert a row at the specified position, shifting the other rows on the bottom
 * if required. */
void evalRow(char *s)
{
    int len = strlen(s);
    row = realloc(row, sizeof(erow));
    row->size = len;
    row->chars = malloc(len + 1);
    memcpy(row->chars, s, len + 1);
    row->hl = NULL;
    row->render = NULL;
    row->rsize = 0;
    /* Update the rendered version and the syntax highlight of a row. */
    UpdateRow(row);
    /* Update the syntax highlighting attributes of the row. */
    UpdateSyntax(row);
}

/* ============================= Terminal update ============================ */



void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;
    memcpy(new + ab->len, s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}

/* This function writes the whole screen using VT100 escape characters
 * starting from the logical state of the editor in the global state 'E'. */
void editorRefreshScreen(struct abuf *ab)
{
    char buf[32];
    abAppend(ab, "\x1b[2J", 4);   /* Clear screen */
    abAppend(ab, "\x1b[?25l", 6); /* Hide cursor. */
    abAppend(ab, "\x1b[H", 3);    /* Go home.     */

    int len = row->rsize;
    int current_color = -1;
    if (len > 0)
    {
        if (len > screencols)
            len = screencols;
        char *c = row->render;
        unsigned char *hl = row->hl;
        int j;
        for (j = 0; j < len; j++)
        {
            if (hl[j] == HL_NONPRINT)
            {
                char sym;
                abAppend(ab, "\x1b[7m", 4);
                if (c[j] <= 26)
                    sym = '@' + c[j];
                else
                    sym = '?';
                abAppend(ab, &sym, 1);
                abAppend(ab, "\x1b[0m", 4);
            }
            else if (hl[j] == HL_NORMAL)
            {
                if (current_color != -1)
                {
                    abAppend(ab, "\x1b[39m", 5);
                    current_color = -1;
                }
                abAppend(ab, c + j, 1);
            }
            else
            {
                int color = hl[j];
                if (color != current_color)
                {
                    char buf[16];
                    int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                    current_color = color;
                    abAppend(ab, buf, clen);
                }
                abAppend(ab, c + j, 1);
            }
        }
    }
    abAppend(ab, "\x1b[39m", 5);
    abAppend(ab, "\x1b[0K", 4);
    abAppend(ab, "\r\n\n", 3);
    abAppend(ab, buf, strlen(buf));
    abAppend(ab, "\x1b[?25h", 6); /* Show cursor. */
}


