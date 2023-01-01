/*
 * @file        libline.h
 * @warning     This is a fork of the original! Do not contact the
 *              authors about bugs in *this* version!
 * @brief       A guerrilla line editing library against the idea that a
 *              line editing lib needs to be 20,000 lines of C code, header only
 * @author      Salvatore Sanfilippo
 * @author      Pieter Noordhuis
 * @author      Richard Howe
 * @license     BSD (included as comment)
 *
 * See libline.c for more information.
 *
 * <ADDED COPYRIGHT>
 *
 * Copyright (c) 2014, Richard Howe <howe.r.j.89@gmail.com>
 *
 * <ORIGINAL LICENSE>
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010, Pieter Noordhuis <pcnoordhuis at gmail dot com>
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

#ifndef LIBLINE_H
#define LIBLINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

	typedef struct line_completions line_completions;
	typedef void (line_completion_callback) (const char *, line_completions *);

	void line_set_vi_mode(int on);
	int  line_get_vi_mode(void);
	void line_set_completion_callback(line_completion_callback *);
	void line_add_completion(line_completions *, const char *);

	char *line_editor(const char *prompt);
	int line_history_add(const char *line);
	int line_history_set_maxlen(int len);
	int line_history_save(const char *filename);
	int line_history_load(const char *filename);
	void line_clearscreen(void);

#ifdef __cplusplus
}
#endif 

#endif /* LIBLINE_H */
