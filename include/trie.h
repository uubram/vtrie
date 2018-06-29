/*
 * Copyright 2016 Bram Gerritsen
 *
 * This file is part of vtrie.
 *
 * vtrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vtrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vtrie.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef TRIE_H
#define TRIE_H

/*
 * Trie structure supporting approximate string matching (substitutions only).
 *
 * The main purpose is to be able to find closely related strings in a list of
 * strings. Strings can be associated with an arbitrary value.
 */

#include <stdbool.h>

typedef enum {
    E_SUCCESS = 0,
    E_OUT_OF_SYNC = -1,
    E_REPLACED = -2
} TrieIterError;

typedef char TRIECHAR;
typedef void TRIEVALUE;

struct TrieItem {
    TRIECHAR *key;
    TRIEVALUE *value;   /* user provided value to associate with string */
    size_t keylen;      /* length of key */
};

/* TrieItems in TrieSearchResult are actual items from the Trie.
 * So do not free (or modify) them. */
struct TrieSearchResult {
    const struct TrieItem *query;
    const struct TrieItem *target;
    int hd;                 /* Hamming distance between query and target */
};

typedef struct TrieRoot TrieRoot;
typedef struct TrieIter TrieIter; /* Iterator for traversing a trie */
typedef struct TrieItem TrieItem;
typedef struct TrieSearchResult TrieSearchResult;

/* Handler to be used for freeing user-provided values which are associated
 * with some of the trie nodes. It has the same signature as the standard C
 * `free` function. */
typedef void (*DeallocHandler) (void*);

/* Creating and destroying a trie */

TrieRoot *trie_new(void);
void trie_free(TrieRoot *root, DeallocHandler dealloc);

/* Get state of a trie */

size_t trie_num_nodes(const TrieRoot *root);
size_t trie_num_items(const TrieRoot *root);
size_t trie_mem_usage(const TrieRoot *root);

/* Getting, setting, and deleting items (i.e. key-value pairs) */

const TrieItem *trie_get_item(const TrieRoot *root, const TRIECHAR *key);
/* 0 means success, -1 error */
int trie_set_item(TrieRoot *root, const TRIECHAR *key, TRIEVALUE *value,
        DeallocHandler dealloc);
/* 0 means success, -1 error */
int trie_del_item(TrieRoot *root, const TRIECHAR *key, DeallocHandler dealloc);

/* Searching through a trie */

bool trie_has_key(const TrieRoot *root, const TRIECHAR *key);
bool trie_has_node(const TrieRoot *root, const TRIECHAR *key);
const TrieItem *trie_longest_prefix(const TrieRoot *root,
        const TRIECHAR *key);
TrieIter *trieiter_suffixes(TrieRoot *root, const TRIECHAR *key);
TrieIter *trieiter_neighbors(TrieRoot *root, const TRIECHAR *key, int maxhd);
TrieIter *trieiter_hammingpairs(TrieRoot *root, int stringlen, int maxhd);
TrieSearchResult *trieiter_next(TrieIter *it);
void trieiter_free(TrieIter *it);
size_t trieiter_len_query(TrieIter *it);
int trieiter_errcode(TrieIter *it);

#endif /* defined TRIE_H */
