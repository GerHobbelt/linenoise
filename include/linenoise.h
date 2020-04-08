/* linenoise.h -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * See linenoise.c for more information.
 *
 * Copyright (c) 2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 */

#ifndef __LINENOISE_H
#define __LINENOISE_H

#define LINENOISE_VERSION "1.0.0"
#define LINENOISE_VERSION_MAJOR 1
#define LINENOISE_VERSION_MINOR 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct linenoiseCompletions linenoiseCompletions;

typedef void(linenoiseCompletionCallback)(const char*, linenoiseCompletions*);
void linenoiseSetCompletionCallback(linenoiseCompletionCallback* fn);
void linenoiseAddCompletion(linenoiseCompletions* lc, const char* str);

char* linenoise(const char* prompt);
void linenoisePreloadBuffer(const char* preloadText);
int linenoiseHistoryAdd(const char* line);
int linenoiseHistorySetMaxLen(int len);
char* linenoiseHistoryLine(int index);
int linenoiseHistorySave(const char* filename);
int linenoiseHistoryLoad(const char* filename);
void linenoiseHistoryFree(void);
void linenoiseClearScreen(void);
void linenoiseSetMultiLine(int ml);
void linenoisePrintKeyCodes(void);
/* the following are extensions to the original linenoise API */
int linenoiseInstallWindowChangeHandler(void);
/* returns type of key pressed: 1 = CTRL-C, 2 = CTRL-D, 0 = other */ 
int linenoiseKeyType(void);

#ifdef __cplusplus
}
#endif

#endif /* __LINENOISE_H */
