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

#include <errno.h>
#ifdef __riscos
#define LINENOISE_ERRNO_CTRLC       4
#define LINENOISE_ERRNO_CTRLD       35
#define LINENOISE_ERRNO_FAILWRITE   5
#define LINENOISE_ERRNO_FAILREAD    6
#define LINENOISE_ERRNO_BADARGS     22
#else
#define LINENOISE_ERRNO_CTRLC       EINTR
#define LINENOISE_ERRNO_CTRLD       EAGAIN
#define LINENOISE_ERRNO_FAILWRITE   EIO
#define LINENOISE_ERRNO_FAILREAD    ENXIO
#define LINENOISE_ERRNO_BADARGS     EINVAL
#endif


typedef struct linenoiseCompletions {
  size_t len;
  char **cvec;
} linenoiseCompletions;

typedef enum linenoiseFailType {
  linenoiseFailCompletion,  /* A completion failed */
  linenoiseFailInsert,      /* An insertion of a character failed (not acceptable/end of buffer) */
  linenoiseFailDelete,      /* Deletion of a character failed (start/end of line) */
  linenoiseFailHistory,     /* No more history to scroll through */
  linenoiseFailMove         /* Move past start or end of line */
} linenoiseFailType;

struct linenoiseConfig;

typedef void(linenoiseCompletionCallback)(const char *, linenoiseCompletions *);
typedef char*(linenoiseHintsCallback)(const char *, int *color, int *bold);
typedef void(linenoiseFreeHintsCallback)(void *);
typedef int(linenoiseInsertCallback)(char c, const char *buffer, int pos);
typedef int(linenoiseFailCallback)(linenoiseFailType fail_type);
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *);
void linenoiseSetHintsCallback(linenoiseHintsCallback *);
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *);
void linenoiseSetInsertCallback(linenoiseInsertCallback *);
void linenoiseSetFailCallback(linenoiseFailCallback *);
void linenoiseAddCompletion(linenoiseCompletions *, const char *);

void linenoiseFree(void *ptr);
void linenoiseShutdown(void);
char *linenoise(const char *prompt);
void linenoiseClearScreen(void);
int linenoiseHistoryAdd(const char *line);
int linenoiseHistorySetMaxLen(int len);
int linenoiseHistorySave(const char *filename);
int linenoiseHistoryLoad(const char *filename);
void linenoisePrintKeyCodes(void);

char *linenoise2(struct linenoiseConfig *config, const char *prompt);
int linenoiseConfigHistoryAdd(struct linenoiseConfig *config, const char *line);
int linenoiseConfigHistorySetMaxLen(struct linenoiseConfig *config, int len);
int linenoiseConfigHistorySave(struct linenoiseConfig *config, const char *filename);
int linenoiseConfigHistoryLoad(struct linenoiseConfig *config, const char *filename);

/* Callbacks */
void linenoiseConfigSetCompletionCallback(struct linenoiseConfig *config, linenoiseCompletionCallback *);
void linenoiseConfigSetHintsCallback(struct linenoiseConfig *config, linenoiseHintsCallback *);
void linenoiseConfigSetFreeHintsCallback(struct linenoiseConfig *config, linenoiseFreeHintsCallback *);
void linenoiseConfigSetInsertCallback(struct linenoiseConfig *config, linenoiseInsertCallback *fn);
void linenoiseConfigSetFailCallback(struct linenoiseConfig *config, linenoiseFailCallback *);

/* Configuration */
void linenoiseSetMultiLine(int ml);
void linenoiseMaskModeEnable(void);
void linenoiseMaskModeDisable(void);
void linenoiseMaskModeChar(char c);
void linenoiseSetMaxLen(int len);

#define LINENOISE_MASKMODE_DISABLED (-1)
#define LINENOISE_MASKMODE_ENABLED ('*')

struct linenoiseConfig *linenoiseNewConfig(void);
void linenoiseFreeConfig(struct linenoiseConfig *config);
void linenoiseConfigSetMultiLine(struct linenoiseConfig *config, int ml);
void linenoiseConfigSetMaskMode(struct linenoiseConfig *config, int maskmode);
void linenoiseConfigSetMaxLen(struct linenoiseConfig *config, int maxlen);

#ifdef __cplusplus
}
#endif

#endif /* __LINENOISE_H */
