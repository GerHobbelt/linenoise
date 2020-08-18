/*
 * Copyright (c) 2020 Shadow Yuan <shadow-yuan@qq.com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
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

#ifndef __LINENOISE_HT_H__
#define __LINENOISE_HT_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct HashNode {
    uint32_t key;
    uint32_t hash;
    void* value;
    struct HashNode* prev;
    struct HashNode* next;
};

struct HashTable {
    uint32_t length;
    uint32_t elems;
    struct HashNode** list;
};

void HashTableInit(struct HashTable * ht);
void HashTableResize(struct HashTable * ht);

// return the old value
void* HashTableInsert(struct HashTable* ht, uint32_t key, void* value);
void* HashTableRemove(struct HashTable* ht, uint32_t key);

void* HashTableLookup(struct HashTable* ht, uint32_t key);

void HashTableEmpty(struct HashTable* ht, void(callback)(uint32_t, void*));

static inline uint32_t GetUInt32ByLittleEndian(const char* ptr) {
  const uint8_t* buffer = (const uint8_t*)(ptr);
  return ((uint32_t)(buffer[0])) |
         ((uint32_t)(buffer[1]) << 8) |
         ((uint32_t)(buffer[2]) << 16) |
         ((uint32_t)(buffer[3]) << 24);
}

#ifdef __cplusplus
}
#endif

#endif  // __LINENOISE_HT_H__
