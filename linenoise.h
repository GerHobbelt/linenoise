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

#ifdef _WIN32
#include <Windows.h>
#include <tchar.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

struct linenoiseCompletions;
typedef struct linenoiseCompletions linenoiseCompletions;

#pragma push_macro("char_t")
#pragma push_macro("charpos_t")
#undef char_t
#undef charpos_t
#ifdef _WIN32
#define char_t TCHAR
#define charpos_t size_t
#else
#define char_t char
#define charpos_t size_t
#endif

/**
 * Completion callback with text and cursor position.
 *
 * @param text text to be completed
 * @param cursor cursor position (byte offset)
 * @param completions suggested completions structure to be filled
 */
typedef void (linenoiseCompletionCallback)(const char_t *text, charpos_t cursor,
    linenoiseCompletions *completions);

/**
 * Sets completion callback.
 * @param callback completion callback
 */
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *callback);

/**
 * Adds completion suggestion with cursor position.
 *
 * @param completions suggested completions structure being filled
 * @param suggestion suggestion to be added (completed word)
 * @param completed_text completed text to be used (not only completed word)
 * @param cursor cursor position to be used (byte offset), or SIZE_MAX to place the cursor at the end
 * @return Returns 0 on success, or -1 on error. See errno for details.
 */
int linenoiseAddCompletion(linenoiseCompletions *completions, const char_t *suggestion, const char_t *completed_text, charpos_t cursor);

/**
 * Prepares the line for custom output. The current text is cleared and the
 * cursor is moved to the new line.
 *
 * Next call to linenoise(const char*) or linenoiseShowPrompt(const char*)
 * refreshes the line.
 *
 * @return Returns 0 on success, or -1 on error. See errno for details.
 */
int linenoiseCustomOutput();

/**
 * Cancels the current line editing.
 *
 * If the line is empty, EINTR is returned from next call to
 * linenoise(const char *).
 *
 * Should be called from SIGINT handler or when linenoise(const char *) is not
 * being called.
 *
 * @return Returns 0 on success, or -1 on error. See errno for details.
 */
int linenoiseCancel();

#ifndef _WIN32
/**
 * Reconfigures the window size.
 *
 * Should be called from SIGWINCH handler or when the linenoise(const char *)
 * is not being called.
 */
int linenoiseUpdateSize();
#endif

#ifdef _WIN32
/**
 * Instructs the linenoise to use asynchronous mode.
 *
 * If in asynchronous mode, returns with EAGAIN when no character is pending.
 *
 * @param is_async if non-zero, mode is asynchronous
 */
void linenoiseSetAsync(int is_async);
#endif

/** Sets the prompt.
 *
 * @param prompt new prompt
 * @return Returns 0 on success, or -1 on error. See errno for details.
 */
int linenoiseSetPrompt(const char_t *prompt);

/**
 * Shows the prompt without reading any character.
 *
 * The method cannot be used for non-ANSI terminal, in which case the method
 * does nothing and returns 0.
 *
 * @return Returns 1 on success, 0 on no action, or -1 on error. See errno for
 * details.
 */
int linenoiseShowPrompt();

/**
 * Checks if there is a pending character to be processed or the line editing
 * has been cancelled.
 *
 * @return Returns 0 when there is nothing to be read, positive number if the
 * linenoise() method should be called to process pending input, or -1 on error.
 * See errno for details. */
int linenoiseHasPendingChar();

/**
 * Clean-up the state when the linenoise() method would not be called any more.
 *
 * @return Returns 0 on success, or -1 on error. See errno for details.
 */
int linenoiseCleanup();

/**
 * Gathers the line from input.
 *
 * @return Text when the full line is read, or NULL in case of error. The errno
 * (Linux) or GetLastError() (Windows) could be 0 if the file descriptor
 * (socket) has been closed remotely, or EINTR (Linux) or ERROR_CANCELLED
 * (Windows) in case of CTRL+C, or EAGAIN/EWOULDBLOCK (Linux) or ERROR_CONTINUE
 * (Windows) in case of non-blocking mode, or ENOMEM (Linux) or
 * ERROR_NOT_ENOUGH_MEMORY (Windows) in case of memory allocation failure, or
 * other values that depend on the file descriptor type. In case of error other
 * than EAGAIN/EWOULDBLOCK or ERROR_CONTINUE, the cursor is moved to new line
 * and TTY raw mode is left automatically.
 */
char_t *linenoise();

/**
 * Adds line of history.
 *
 * @param line line to be added
 * @return Returns 0 on success, or -1 on error. See errno for details.
 */
int linenoiseHistoryAdd(const char_t *line);

/**
 * Sets maximum number of lines in history.
 *
 * @param len number of history lines
 * @return Returns 0 on success, or -1 on error. See errno for details.
 */
int linenoiseHistorySetMaxLen(int len);

/**
 * Saves the current history to file.
 *
 * @param filename history file name
 * @return Returns 0 on success, or -1 on error. See errno for details.
 */
int linenoiseHistorySave(const char_t *filename);

/**
 * Loads the history from the file.
 *
 * @param filename history file name
 * @return Returns 0 on success, or -1 on error. See errno for details.
 */
int linenoiseHistoryLoad(const char_t *filename);

/**
 * Clears the screen.
 *
 * @return Returns 0 on success, or -1 on error. See errno for details.
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

#pragma pop_macro("charpos_t")
#pragma pop_macro("char_t")

#ifdef __cplusplus
}
#endif

#endif /* __LINENOISE_H */
