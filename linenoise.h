/* linenoise.h -- VERSION 1.0
 *
 * Guerrilla line editing library against the idea that a line editing lib
 * needs to be 20,000 lines of C code.
 *
 * See linenoise.c for more information.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2014, Salvatore Sanfilippo <antirez at gmail dot com>
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
 */

#ifndef __LINENOISE_H
#define __LINENOISE_H

#ifdef __cplusplus
extern "C" {
#endif


#define LINENOISE_COMPLETION_ATTR_INTR ((unsigned int)(1 << 0))
#define LINENOISE_COMPLETION_ATTR_NOSP ((unsigned int)(1 << 1))

typedef struct linenoiseCompletions {
  size_t attr;
  size_t len;
  char **cvec;
} linenoiseCompletions;

typedef void(linenoiseCompletionCallback)(const char *, size_t , linenoiseCompletions *);
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *);
void linenoiseAddCompletion(linenoiseCompletions *, const char *);

char *linenoise(const char *prompt);
void linenoiseClearScreen(void);
void linenoiseSetMultiLine(int ml);
//void linenoisePrintKeyCodes(void);

int *linenoiseInputFD();
int *linenoiseOutputFD();
int *linenoiseErrorFD();

typedef size_t (linenoisePrevCharLen)(const char *buf, size_t buf_len, size_t pos, size_t *col_len);
typedef size_t (linenoiseNextCharLen)(const char *buf, size_t buf_len, size_t pos, size_t *col_len);
typedef size_t (linenoiseReadCode)(int fd, char *buf, size_t buf_len, int* c);
typedef size_t (linenoisePrevWordLen)(const char *buf, size_t buf_len, size_t pos, size_t *col_len);
typedef size_t (linenoiseNextWordLen)(const char *buf, size_t buf_len, size_t pos, size_t *col_len);

void linenoiseSetEncodingFunctions(
    linenoisePrevCharLen *prevCharLenFunc,
    linenoiseNextCharLen *nextCharLenFunc,
    linenoiseReadCode *readCodeFunc,
    linenoisePrevWordLen *prevWordLenFunc,
    linenoiseNextWordLen *nextWordLenFunc);

typedef enum {
    LINENOISE_HISTORY_OP_NEXT,
    LINENOISE_HISTORY_OP_PREV,
    LINENOISE_HISTORY_OP_DELETE,
    LINENOISE_HISTORY_OP_INIT,
    LINENOISE_HISTORY_OP_SEARCH,
} historyOp;

typedef const char *(linenoiseHistoryCallback)(const char *buf, int *history_index, historyOp op);

void linenoiseSetHistoryCallback(linenoiseHistoryCallback *callback);

typedef int(linenoisePropertyCheckCallback)(const char *str, size_t pos);

void linenoiseSetPropertyCheckCallback(linenoisePropertyCheckCallback *callback,
                                       const char *const strs[], size_t strc);

typedef const char *(linenoiseHighlightCallback)(const char *buf, size_t buf_len, size_t *ret_len);

void linenoiseSetHighlightCallback(linenoiseHighlightCallback *callback);

#ifdef __cplusplus
}
#endif

#endif /* __LINENOISE_H */
