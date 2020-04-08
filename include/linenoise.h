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

//#define LINENOISE_VERSION "1.0.0"
//#define LINENOISE_VERSION_MAJOR 1
//#define LINENOISE_VERSION_MINOR 1

// DBJ 2020-04-08
#define LINENOISE_VERSION "2.0.0"
#define LINENOISE_VERSION_MAJOR 2
#define LINENOISE_VERSION_MINOR 0
#define LINENOISE_VERSION_PATCH 0

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

/*-----------------------------------------------------------------------------------------*/

#define vt100_reset "\x1b[0m"
#define vt100_bright "\x1b[1m"
#define vt100_dim "\x1b[2m"
#define vt100_underscore "\x1b[4m"
#define vt100_blink "\x1b[5m"
#define vt100_reverse "\x1b[7m"
#define vt100_hidden "\x1b[8m"

#define vt100_black "\x1b[30m"
#define vt100_red "\x1b[31m"
#define vt100_green "\x1b[32m"
#define vt100_yellow "\x1b[33m"
#define vt100_blue "\x1b[34m"
#define vt100_magenta "\x1b[35m"
#define vt100_cyan "\x1b[36m"
#define vt100_white "\x1b[37m"

#define vt100_bgblack "\x1b[40m"
#define vt100_bgred "\x1b[41m"
#define vt100_bggreen "\x1b[42m"
#define vt100_bgyellow "\x1b[43m"
#define vt100_bgblue "\x1b[44m"
#define vt100_bgmagenta "\x1b[45m"
#define vt100_bgcyan "\x1b[46m"
#define vt100_bgwhite "\x1b[47m"

/*-----------------------------------------------------------------------------------------*/
/*
most of the time not needed because WIN10 2020 Q2 consoles is pre set to do VT100 esc codes

requires:

#include <stdio.h>
#include <wchar.h>
#include <windows.h>
*/
#ifdef _WIN32
#include <windows.h>
inline void set_output_mode_to_handle_virtual_terminal_sequences() 
{
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut == INVALID_HANDLE_VALUE) {
    perror("GetStdHandle() failed");
    return;
  }

  DWORD dwMode = 0;
  if (!GetConsoleMode(hOut, &dwMode)) {
    perror("GetConsoleMode() failed");
    return;
  }

  dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  if (!SetConsoleMode(hOut, dwMode)) {
    perror("SetConsoleMode() failed");
    return;
  }
}
#endif /// _WIN32
#endif /* __LINENOISE_H */
