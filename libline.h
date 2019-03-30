/*
 * @file        libline.h
 * @brief       A guerrilla line editing library against the idea that a
 *              line editing lib needs to be 20,000 lines of C code, header only
 * @author      Salvatore Sanfilippo
 * @author      Pieter Noordhuis
 * @author      Richard James Howe
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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#ifndef LIBLINE_H
#define LIBLINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef struct line_completions line_completions;
typedef void (line_completion_callback) (const char *, size_t pos, line_completions *);

typedef struct {
	line_completion_callback *completion_callback;
	char **history;
	int vi_mode, vi_escape;
	int history_max_len, history_len, history_on;
	int in, out; /* file descriptors */
	int rawmode;
} libline_t;

void line_set_vi_mode(libline_t *, int on);
int  line_get_vi_mode(libline_t *);
void line_set_completion_callback(libline_t *, line_completion_callback *);
void line_add_completion(libline_t *, line_completions *, const char *);

char *line_editor(libline_t *, const char *prompt);
int line_history_add(libline_t *, const char *line);
int line_history_set_maxlen(libline_t *, int len);
int line_history_save(libline_t *, const char *filename);
int line_history_load(libline_t *, const char *filename);
int line_clearscreen(libline_t*);

int line_cleanup(libline_t *);
int line_initialize(libline_t *);

#ifdef __cplusplus
}
#endif 
#endif /* LIBLINE_H */
