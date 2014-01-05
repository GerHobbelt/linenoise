/* linenoise.h -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * See linenoise.c for more information.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010, Pieter Noordhuis <pcnoordhuis at gmail dot com>
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
 */

#ifndef __LINENOISE_H
#define __LINENOISE_H

#ifdef __cplusplus
extern "C"
{
#endif

struct linenoiseCompletions;
typedef struct linenoiseCompletions linenoiseCompletions;

/**
 * Completion callback with text and cursor position.
 *
 * @param text text to be completed
 * @param cursor cursor position
 * @param completions suggested completions structure to be filled
 */
typedef void(linenoiseCompletionCallback)(const char *text, size_t cursor, linenoiseCompletions *completions);

/**
 * Sets completion callback.
 * @param callback completion callback
 */
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *callback);

/**
 *  * Adds completion suggestion with cursor position.
 *
 * @param completions suggested completions structure being filled
 * @param completion completion to be added
 * @param cursor cursor position to be used, or SIZE_MAX to place the cursor at the end
 */
void linenoiseAddCompletion(linenoiseCompletions *completions, char *completion, size_t cursor);

/**
 * Prepares the line for custom output. The current text is cleared and the
 * cursor is moved to the new line.
 *
 * Next call to linenoise(const char*) or linenoiseShowPrompt(const char*)
 * refreshes the line.
 */
void linenoiseCustomOutput();

/**
 * Cancels the current line editing.
 *
 * If the line is empty, EINTR is returned from next call to
 * linenoise(const char *).
 *
 * Should be called from SIGINT handler or when linenoise(const char *) is not
 * being called.
 */
void linenoiseCancel();

/**
 * Reconfigures the window size.
 *
 * Should be called from SIGWINCH handler or when the linenoise(const char *)
 * is not being called.
 */
void linenoiseUpdateSize();

/**
 * Shows the prompt without reading any character.
 *
 * The method cannot be used for non-ANSI terminal, in which case the method
 * returns -1 and sets errno to EBADF.
 *
 * Returns 0 on success, or -1 on error. See errno for details.
 */
int linenoiseShowPrompt(const char *prompt);

/**
 * Gathers the line from input.
 *
 * @param prompt the prompt to be used
 * @return Text when the full line is read, or NULL in case of error. The errno
 * could be 0 if the file descriptor has been closed, or EINTR in case of
 * CTRL+C, or ENOMEM in case of memory allocation failure, or other values
 * that depend on the file descriptor type.
 */
char *linenoise(const char *prompt);

/**
 * Adds line of history.
 *
 * @param line line to be added
 * @return Zero on success, -1 on error. See errno for error detail.
 */
int linenoiseHistoryAdd(const char *line);

/**
 * Sets maximum number of lines in history.
 *
 * @param len number of history lines
 * @return Zero on success, -1 on error. See errno for error detail.
 */
int linenoiseHistorySetMaxLen(int len);

/**
 * Saves the current history to file.
 *
 * @param filename history file name
 * @return Zero on success, -1 on error. See errno for error detail.
 */
int linenoiseHistorySave(char *filename);

/**
 * Loads the history from the file.
 *
 * @param filename history file name
 * @return Zero on success, -1 on error. See errno for error detail.
 */
int linenoiseHistoryLoad(char *filename);

/**
 * Clears the screen.
 *
 * @return Zero on success, -1 on error. See errno for error detail.
 */
int linenoiseClearScreen(void);

/**
 * Sets single or multi-line.
 *
 * The editor is set to single-line initially.
 *
 * @param ml non-zero to use multi-line mode, zero for single-line.
 */
void linenoiseSetMultiLine(int ml);

#ifdef __cplusplus
}
#endif

#endif /* __LINENOISE_H */
