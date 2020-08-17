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

#include "ht.h"

//
// hash function
//

static inline uint32_t GetUInt32ByLittleEndian(const char* ptr) {
  const uint8_t* buffer = (const uint8_t*)(ptr);
  return ((uint32_t)(buffer[0])) |
         ((uint32_t)(buffer[1]) << 8) |
         ((uint32_t)(buffer[2]) << 16) |
         ((uint32_t)(buffer[3]) << 24);
}

uint32_t CalcHash(const char* data, size_t n, uint32_t seed) {
    const uint32_t m = 0xc6a4a793;
    const uint32_t r = 24;
    const char* limit = data + n;
    uint32_t h = seed ^ (n * m);

    // Pick up four bytes at a time
    while (data + 4 <= limit) {
        uint32_t w = GetUInt32ByLittleEndian(data);
        data += 4;
        h += w;
        h *= m;
        h ^= (h >> 16);
    }

    // Pick up remaining bytes
    switch (limit - data) {
    case 3:
        h += (uint32_t)(data[2]) << 16;
    case 2:
        h += (uint32_t)(data[1]) << 8;
    case 1:
        h += (uint32_t)(data[0]);
        h *= m;
        h ^= (h >> r);
        break;
    }
    return h;
}

//
// list
//

struct HashNode* HashNodeListMatchKey(struct HashNode* head, uint32_t key) {
    struct HashNode* entry = NULL;
    if (head->key == key) {
        return head;
    }
    entry = head->next;
    while (entry != head) {
        if (entry->key == key) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

void HashNodeListPushback(struct HashNode* head, struct HashNode* new_item) {
    struct HashNode* last = head->prev;
    last->next = new_item;
    new_item->prev = last;
    new_item->next = head;
    head->prev = new_item;
}

struct HashNode* HashNodeListPopback(struct HashNode* head) {
    struct HashNode* second = 0;
    struct HashNode* removed = 0;

    if (!head) return NULL;
    if (head->prev == head) return head;

    removed = head->prev;
    second = removed->prev;

    second->next = head;
    head->prev = second;
    return removed;
}

int HashNodeListRemoveNode(struct HashNode* item) {
    struct HashNode* prior;
    struct HashNode* next;

    if (item->next == item) {  // It's empty.
        return 0;
    }

    prior = item->prev;
    next = item->next;

    prior->next = next;
    next->prev = prior;
    return 1;
}

//
// hash table
//

void HashTableInit(struct HashTable * ptr) {
    ptr->length = 0;
    ptr->elems = 0;
    ptr->list = NULL;
    ResizeHashTable(ptr);
}

void HashTableResize(struct HashTable * ptr) {
    uint32_t new_length = 4;
    void** new_list = NULL;
    uint32_t count = 0;

    while (new_length < ptr->elems) {
      new_length *= 2;
    }

    new_list = malloc(sizeof(struct HashNode*) * new_length);
    memset(new_list, 0, sizeof(struct HashNode*) * new_length);

    for (uint32_t i = 0; i < ptr->length; i++) {
        struct HashNode* h = ptr->list[i];
        while (h != NULL) {
            struct HashNode* entry = HashNodeListPopback(h);

            uint32_t hash = entry->hash;
            struct HashNode** ptr = &new_list[hash & (new_length - 1)];
            if (*ptr != NULL) {
                HashNodeListPushback(*ptr, entry);
            } else {
                entry->prev = entry->next = entry;
                *ptr = entry;
            }

            count++;

            if (entry == h) {
                break;
            }
        }
    }
    assert(ptr->elems == count);
    free(ptr->list);
    ptr->list = new_list;
    ptr->length = new_length;
}

void* HashTableInsert(struct HashTable* ht, uint32_t key, void* value) {
    uint32_t hash = CalcHash(&key, sizeof(key), 0);
    struct HashNode** head = &ht->list[hash & (ht->length - 1)];
    struct HashNode* obj = NULL;
    void* old_value = NULL;
    if (*head) {
        obj = HashNodeListMatchKey(*head, key); // find one, replace it
        if (obj) {
            old_value = obj->value;
            obj->value = value;
            return old_value;
        }
        obj = malloc(sizeof(struct HashNode));
        obj->hash = hash;
        obj->key = key;
        obj->value = value;
        obj->prev = obj->next = NULL;
        HashNodeListPushback(*head, obj);
    } else {
        obj = malloc(sizeof(struct HashNode));
        obj->hash = hash;
        obj->key = key;
        obj->value = value;
        obj->prev = obj->next = obj;
        *head = obj;
    }

    ht->elems++;

    if (ht->elems > ht->length) {
        // make sure average linked list length (<= 1).
        HashTableResize(ht);
    }

    return old_value;
}

void* HashTableRemove(struct HashTable* ht, uint32_t key) {
    uint32_t hash = CalcHash(&key, sizeof(key), 0);
    struct HashNode** head = &ht->list[hash & (ht->length - 1)];
    struct HashNode* obj = NULL;
    void* old_value = NULL;
    if (*head == NULL) return NULL;

    obj = HashNodeListMatchKey(*head, key);
    if (!obj) return NULL;

    old_value = obj->value;
    if (!HashNodeListRemoveNode(obj)) { // Remove head
        free(*head);
        *head = NULL;
    } else {
        free(obj);
    }
    ht->elems--;
    return old_value;
}

void* HashTableLookup(struct HashTable* ht, uint32_t key) {
    uint32_t hash = CalcHash(&key, sizeof(key), 0);
    struct HashNode* head = ht->list[hash & (ht->length - 1)];
    struct HashNode* obj = NULL;
    if (head) {
        obj = HashNodeListMatchKey(head, key);
        if (obj) {
            return obj->value;
        }
    }
    return NULL;
}
