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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "trie.h"

/*
 * Flags used to set the status of nodes/trie.
 */
#define TRIE_EXPLORED      0x0001

/* Define the fields of a TrieNode */
#define TrieNode_FIELDS                                             \
    struct TrieItem item;                                           \
    struct TrieNode *parent;                                        \
    struct TrieNode *sibling;   /* points to next sibling */        \
    struct TrieNode *child;     /* first in a list of children */   \
    TRIECHAR ch;                                                    \
    int flags;

struct ListNode {
    void *value;
    struct ListNode *next;
};

struct TrieNode {
    TrieNode_FIELDS
};

struct TrieRoot {
    TrieNode_FIELDS         /* Root node can hold empty string */
    size_t num_nodes;       /* Number of nodes in the Trie */
    size_t num_items;       /* Number of items in the Trie */
    size_t memsize;         /* Size of trie in memory in bytes */
    long long state_id;     /* identifier for current state of the Trie */
    TrieIter *dirty_iter;   /* active dirty iterator, NULL if nothing active */
};

struct TrieIterState {
    struct TrieNode *node;  /* current node */
    struct TrieNode *query; /* node corresponding to current query string */
    int hd;                 /* Hamming distance between `node` and `query` */
    int depth;              /* depth of `node` in the trie */
};

typedef TrieSearchResult * (*TrieIterNextFunc) (TrieIter *);

struct TrieIter {
    struct TrieRoot *root;
    struct TrieIterState *head;
    struct TrieIterState *fill; /* points at new position to be filled */
    struct TrieIterState *tail;
    int maxhd;
    int target_depth;
    int len_query;
    bool is_dirty;
    long long trie_state_id;    /* state_id of Trie at iter creation */
    int errcode;
    struct ListNode *stack;
    TrieIterNextFunc next;
};

typedef struct ListNode ListNode;
typedef struct TrieIterState TrieIterState;
typedef struct TrieNode TrieNode;

static TRIECHAR *
duplicate_string(const TRIECHAR *s, size_t n)
{
    TRIECHAR *dup = safe_malloc(sizeof(*dup) * n);
    return (TRIECHAR *) memcpy(dup, s, sizeof(*dup) * n);
}

/*
 * Push value onto stack.
 *
 * stack_ptr: pointer to stack (/list). This pointer is modified.
 * value: value to push onto the stack.
 */
static void
stack_push(ListNode **stack_ptr, void *value)
{
    if (stack_ptr == NULL)
        return;

    ListNode *node = safe_malloc(sizeof(*node));
    node->value = value;

    /* push node onto the stack (i.e. prepend it to the list) */
    node->next = *stack_ptr;
    *stack_ptr = node;
}

static void *
stack_pop(ListNode **stack_ptr)
{
    if (stack_ptr == NULL || *stack_ptr == NULL)
        return NULL;

    ListNode *node = *stack_ptr;
    *stack_ptr = (*stack_ptr)->next;
    void *retval = node->value;
    free(node);
    return retval;
}

static void
trieiterstate_init(TrieIterState *state, TrieNode *node, TrieNode *query,
        int hd, int depth)
{
    state->node = node;
    state->query = query;
    state->hd = hd;
    state->depth = depth;
}

/*
 * NOTE: do not use a popped state after a push! i.e. a push invalidates a
 * pop.
 *
 * e.g.:
 * TrieIterState *s = trieiter_pop_state(it);
 * trieiter_push_state(...); // This overwrites *s!
 *
 */
static void
trieiter_push_state_unsafe(TrieIter *it, TrieNode *node, TrieNode *query,
        int hd, int depth)
{
    trieiterstate_init(it->fill, node, query, hd, depth);
    it->fill++;
}

static void
trieiter_push_state(TrieIter *it, TrieNode *node, TrieNode *query,
        int hd, int depth)
{
    if (it->fill == it->tail){
        size_t size = it->tail - it->head;
        it->head = safe_realloc(it->head, 2 * size * sizeof(*it->head));
        it->tail = it->head + 2 * size;
        it->fill = it->head + size;
    }
    trieiter_push_state_unsafe(it, node, query, hd, depth);
}

static TrieIterState * 
trieiter_pop_state(TrieIter *it)
{
    if (it->fill == it->head)
        return NULL;

    /* The data fill points to is considered open for writing. So after this
     * pop, fill is pointing to the returned state.
     * Pushing overwrites the most recent pop. */
    it->fill--;
    return it->fill;
}

static void trie_reset(TrieNode *node);

static TrieIter *
trieiter_new(TrieRoot *root, int num_states, int maxhd, int target_depth,
        int len_query, ListNode *stack, TrieIterNextFunc next, bool is_dirty)
{
    if (num_states < 1 || maxhd < 0 || target_depth < 0)
        return NULL;

    TrieIter *it = safe_malloc(sizeof(*it));
    it->root = root;
    it->head = safe_malloc(sizeof(*it->head) * num_states);
    it->fill = it->head;
    it->tail = it->head + num_states;
    it->maxhd = maxhd;
    it->target_depth = target_depth;
    it->len_query = len_query;
    it->is_dirty = is_dirty;
    it->trie_state_id = root->state_id;
    it->errcode = E_SUCCESS;
    it->stack = stack;
    it->next = next;

    if (is_dirty){
        if (root->dirty_iter != NULL){
            /* Trie may be dirty from the other iterator */
            trie_reset((TrieNode *)root);
        }
        root->dirty_iter = it;
    }

    return it;
}

TrieSearchResult *
trieiter_next(TrieIter *it)
{
    if (it == NULL)
        return NULL;

    /* Check if Trie had nodes added or removed since this iterator was
     * created. Without this test, iterators could have dangling pointers (in
     * case of node removal), or fail to traverse parts of the trie (in case of
     * node addition).*/
    if (it->trie_state_id != it->root->state_id){
        it->errcode = E_OUT_OF_SYNC;
        return NULL;
    }

    /* Check if dirty iter was replaced by another dirty iter. */
    if (it->is_dirty && it->root->dirty_iter != it){
        it->errcode = E_REPLACED;
        return NULL;
    }

    return it->next(it);
}

int
trieiter_errcode(TrieIter *it)
{
    return it->errcode;
}

size_t
trieiter_len_query(TrieIter *it)
{
    return it->len_query;
}

static void
trieitem_free(TrieItem *item, DeallocHandler dealloc)
{
    if (item == NULL)
        return;

    if (item->key != NULL){
        free(item->key);
        item->key = NULL;
    }

    if (item->value != NULL && dealloc != NULL){
        dealloc(item->value);
        item->value = NULL;
    }

    item->keylen = 0;
}

/* Reset flags field of all nodes below `node` */
static void
trie_reset(TrieNode *node)
{
    if (node == NULL)
        return;

    if (node->child != NULL)
        trie_reset(node->child);

    if (node->sibling != NULL)
        trie_reset(node->sibling);

    node->flags = 0;
}

/*
 * NOTE: it->stack will be handled by popping all its values, assuming the
 * values on the stack do not need to be freed.
 */
void trieiter_free(TrieIter *it)
{
    if (it == NULL)
        return;

    if (it == it->root->dirty_iter){
        trie_reset((TrieNode *)it->root);
        it->root->dirty_iter = NULL;
    }

    free(it->head);
    while(stack_pop(&it->stack)!=NULL);
    free(it);
}

static TrieSearchResult *
triesearchresult_new(const TrieNode *query, const TrieNode *target, int hd)
{
    TrieSearchResult *result = safe_calloc(1, sizeof(*result));
    if (query != NULL)
        result->query = (const TrieItem *)&query->item;
    if (target != NULL)
        result->target = (const TrieItem *)&target->item;
    result->hd = hd;
    return result;
}

static TrieNode *
trienode_new(TRIECHAR *key, TRIEVALUE *value, size_t keylen, TrieNode *parent,
        TrieNode *sibling, TrieNode *child, TRIECHAR ch, int flags)
{
    TrieNode *node = safe_malloc(sizeof(*node));
    node->item.key = key;
    node->item.value = value;
    node->item.keylen = keylen;
    node->parent = parent;
    node->sibling = sibling;
    node->child = child;
    node->ch = ch;
    node->flags = flags;
    return node;
}

static void
trienode_free(TrieNode *node, DeallocHandler dealloc)
{
    if (node != NULL){
        trieitem_free(&node->item, dealloc);
        free(node);
    }
}

static TrieNode *
trienode_get_child(const TrieNode *node, TRIECHAR ch)
{
    TrieNode *child = node->child;
    while (child != NULL && child->ch != ch)
        child = child->sibling;
    return child;
}

/*
 * Removes a child denoted by ch from a node. The child will only be removed if
 * it has no children.
 */
static void
trienode_remove_child(TrieNode *node, TRIECHAR ch, DeallocHandler dealloc)
{
    TrieNode *child = node->child;
    TrieNode *prev = NULL;
    while (child != NULL && child->ch != ch){
        prev = child;
        child = child->sibling;
    }

    if (child != NULL && child->child == NULL){
        if (child == node->child)
            node->child = child->sibling;

        if (prev != NULL)
            prev->sibling = child->sibling;

        trienode_free(child, dealloc);
    }
}

static const TrieNode *
trie_get_node(const TrieRoot *root, const TRIECHAR *key)
{
    if (root == NULL)
        return NULL;

    TrieNode *node = (TrieNode *)root;
    for(const TRIECHAR *ch = key; node != NULL && *ch != '\0'; ch++)
        node = trienode_get_child(node, *ch);

    return node;
}

size_t
trie_num_nodes(const TrieRoot *root)
{
    if (root == NULL)
        return -1;
    return root->num_nodes;
}

size_t
trie_num_items(const TrieRoot *root)
{
    if (root == NULL)
        return -1;
    return root->num_items;
}

size_t
trie_mem_usage(const TrieRoot *root)
{
    return root->memsize;
}

bool
trie_has_key(const TrieRoot *root, const TRIECHAR *key)
{
    const TrieNode *node = trie_get_node(root, key);
    return node != NULL && node->item.key != NULL;
}

bool
trie_has_node(const TrieRoot *root, const TRIECHAR *key)
{
    return trie_get_node(root, key) != NULL;
}

const TrieItem *
trie_get_item(const TrieRoot *root, const TRIECHAR *key)
{
    const TrieNode *node = trie_get_node(root, key);
    if (node == NULL || node->item.key == NULL)
        return NULL;
    
    return &node->item;
}

const TrieItem *
trie_longest_prefix(const TrieRoot *root, const TRIECHAR *key)
{
    if (root == NULL)
        return NULL;

    TrieNode *node = (TrieNode *)root;
    const TrieItem *res = NULL;
    if (root->item.key != NULL)
        res = &root->item;

    for(const TRIECHAR *ch = key; node != NULL && *ch != '\0'; ch++){
        node = trienode_get_child(node, *ch);
        if (node != NULL && node->item.key != NULL)
            res = &node->item;
    }

    return res;
}

TrieRoot *
trie_new()
{
    TrieRoot *root = safe_malloc(sizeof(*root));

    /* TrieNode specific fields */
    root->item.key = NULL;
    root->item.value = NULL;
    root->item.keylen = 0;
    root->parent = NULL;
    root->sibling = NULL;
    root->child = NULL;
    root->ch = '\0';
    root->flags = 0;

    /* Trie (root) specific fields */
    root->num_nodes = 0;
    root->num_items = 0;
    root->memsize = sizeof(*root);
    root->state_id = 0;
    root->dirty_iter = NULL;
    return root;
}

/*
 * Free memory of a trie.
 *
 * node: this node and everything below it will be removed from the trie.
 * dealloc: function used to deallocate memory of any non-NULL value a trie
 * node may reference.
 */
static void
_trie_free(TrieNode *node, DeallocHandler dealloc)
{
    if (node == NULL)
        return;

    if (node->child != NULL)
        _trie_free(node->child, dealloc);

    if (node->sibling != NULL)
        _trie_free(node->sibling, dealloc);

    trienode_free(node, dealloc);
}

void
trie_free(TrieRoot *root, DeallocHandler dealloc)
{
    _trie_free((TrieNode *)root, dealloc);
}

/*
 * Insert key into the trie and associate it with the provided value.
 *
 * root: root of the trie.
 * key: key to insert.
 * value: value to associate key with in the trie.
 * dealloc: handler to use to free memory of any value with which the key 
 * might already be associated. NULL will be interpreted to mean to not
 * deallocate memory of a pre-existing value.
 *
 * @return: 0 means successful insertion of the key, and a non-zero value
 * means insertion failed. 
 */
int
trie_set_item(TrieRoot *root, const TRIECHAR *key, TRIEVALUE *value,
        DeallocHandler dealloc)
{
    if (root == NULL || key == NULL)
        return -1;

    TrieNode *node = (TrieNode *)root;
    const TRIECHAR *ch;
    for (ch = key; *ch != '\0'; ch++){
        TrieNode *child = trienode_get_child(node, *ch);
        if (child == NULL){
            node->child = trienode_new(
                    NULL,           /* key */
                    NULL,           /* value */
                    0,              /* keylen */
                    node,           /* parent */
                    node->child,    /* sibling */
                    NULL,           /* child */
                    *ch,
                    0               /* flags */
                    );
            child = node->child;
            root->num_nodes++;
            root->memsize += sizeof(*child);
        }
        node = child;
    }

    if (node->item.key == NULL){
        root->num_items++;
        /* update state_id because one or more nodes have been added */
        root->state_id++;
    }else{
        root->memsize -= sizeof(TRIECHAR) * (node->item.keylen + 1);
        trieitem_free(&node->item, dealloc);
    }

    size_t keylen = ch - key;
    node->item.key = duplicate_string(key, keylen + 1);
    node->item.keylen = keylen;
    node->item.value = value;

    root->memsize += sizeof(TRIECHAR) * (keylen + 1);

    return 0;
}

/*
 * Removes key from the trie, including any nodes leading up to it unless
 * this would break the trie for other strings.
 *
 * 0 is returned in case of successful removal of the string from the trie.
 */
int
trie_del_item(TrieRoot *root, const TRIECHAR *key, DeallocHandler dealloc)
{
    TrieNode *parent;

    if (root == NULL || key == NULL)
        return -1;

    TrieNode *node = (TrieNode *)trie_get_node(root, key);

    if (node == NULL || node->item.key == NULL)
        return -1;

    root->memsize -= sizeof(TRIECHAR) * (node->item.keylen + 1);
    trieitem_free(&node->item, dealloc);
    root->num_items--;

    while (node != (TrieNode *)root && node->child == NULL &&
            node->item.key == NULL){
        parent = node->parent;
        trienode_remove_child(parent, node->ch, dealloc);
        root->memsize -= sizeof(*node);
        node = parent;
        root->num_nodes--;
    }

    /* update state_id because one or more nodes have been removed */
    root->state_id++;
    return 0;
}

static TrieSearchResult *
trieiter_suffixes_next(TrieIter *it)
{
    TrieIterState *state = NULL;
    while ((state = trieiter_pop_state(it)) != NULL){
        TrieSearchResult *result = NULL;
        if (state->node->item.key != NULL)
            result = triesearchresult_new(state->query, state->node, 0);

        TrieNode *query = state->query;
        TrieNode *child = state->node->child;
        int depth = state->depth;
        for (; child != NULL; child = child->sibling)
                trieiter_push_state(it, child, query, 0, depth + 1);

        if (result != NULL)
            return result;
    }
    return NULL;
}

TrieIter *
trieiter_suffixes(TrieRoot *root, const TRIECHAR *key)
{
    if (root == NULL || key == NULL)
        return NULL;

    TrieNode *query = (TrieNode *)trie_get_node(root, key);

    if (query == NULL)
        return NULL;

    TrieIter *it = trieiter_new(
            root,
            1,          /* number of states */
            0,          /* maxhd (not used) */
            0,          /* target_depth (not used) */
            strlen(key),/* len_query (not used) */
            NULL,       /* stack (not used) */
            trieiter_suffixes_next,
            false       /* is_dirty */
            );

    if (it == NULL)
        return NULL;

    trieiter_push_state(it, query, query, 0, 0);
    return it;
}

static TrieSearchResult *
trieiter_neighbors_next(TrieIter *it)
{
    TrieIterState *state = NULL;
    TrieNode *query, *target, *child;
    int hd, depth;
    while ((state = trieiter_pop_state(it)) != NULL){
        if (state->depth == it->target_depth){
            if (state->node->item.key == NULL || state->hd == 0)
                continue;
            query = state->query;
            target = state->node;
            hd = state->hd;
            return triesearchresult_new(query, target, hd);
        }

        query = state->query;
        child = state->node->child;
        depth = state->depth;
        hd = state->hd;
        for (; child != NULL; child = child->sibling){
            if (child->ch == *(query->item.key + depth))
                trieiter_push_state(it, child, query, hd, depth + 1);
            else if (hd < it->maxhd)
                trieiter_push_state(it, child, query, hd + 1, depth + 1);
        }
    }
    return NULL;
}

TrieIter *
trieiter_neighbors(TrieRoot *root, const TRIECHAR *key, int maxhd)
{
    if (root == NULL || key == NULL || maxhd < 1)
        return NULL;

    TrieNode *query = (TrieNode *)trie_get_node(root, key);

    if (query == NULL || query->item.key == NULL)
        return NULL;

    TrieIter *it = trieiter_new(
            root,
            1,                          /* number of states */
            maxhd,
            query->item.keylen,        /* target_depth */
            query->item.keylen,        /* len_query (not used) */
            NULL,                       /* stack (not used) */
            trieiter_neighbors_next,
            false                       /* is_dirty */
            );

    if (it == NULL)
        return NULL;

    trieiter_push_state(it, (TrieNode *)it->root, query, 0, 0);
    return it;
}

static void
trie_find_all_strings(TrieNode *node, int depth, ListNode **targets)
{
    if (depth == 0 && node->item.key != NULL)
        stack_push(targets, node);
    else if (depth > 0)
        for(node = node->child; node != NULL; node = node->sibling)
            trie_find_all_strings(node, depth - 1, targets);
}

static TrieSearchResult *
trieiter_hammingpairs_next(TrieIter *it)
{
    TrieIterState *state = NULL;
    TrieNode *query, *target, *child, *node;
    int hd, depth, n_children, n_explored;
    while((state = trieiter_pop_state(it)) != NULL || it->stack != NULL){
        if (state == NULL){ /* get next query string */
            TrieNode *query = stack_pop(&it->stack);
            query->flags |= TRIE_EXPLORED;
            trieiter_push_state(it, (TrieNode *)it->root, query, 0, 0);
            continue;
        }

        if (state->depth == it->target_depth){
            if (state->node->item.key == NULL){
                state->node->flags |= TRIE_EXPLORED;
                continue;
            }
            query = state->query;
            target = state->node;
            hd = state->hd;
            return triesearchresult_new(query, target, hd);
        }

        /* save values from state, because pushes invalidates current state */
        node = state->node;
        query = state->query;
        hd = state->hd;
        depth = state->depth;
        n_children = 0;
        n_explored = 0;
        for (child = node->child; child != NULL; child = child->sibling){
            n_children++;
            if ((child->flags & TRIE_EXPLORED) == TRIE_EXPLORED){
                n_explored++;
            }
            else{
                if (child->ch == *(query->item.key + depth))
                    trieiter_push_state(it, child, query, hd, depth+1);
                else if (hd < it->maxhd)
                    trieiter_push_state(it, child, query, hd+1, depth+1);
            }
        }
        if (n_children == n_explored)
            node->flags |= TRIE_EXPLORED;
    }
    return NULL;
}

TrieIter *
trieiter_hammingpairs(TrieRoot *root, int keylen, int maxhd)
{
    if (root == NULL || keylen <= 0)
        return NULL;

    ListNode *targets = NULL;
    trie_find_all_strings((TrieNode *)root, keylen, &targets);

    TrieIter *it = trieiter_new(
            root,
            1,          /* number of states */
            maxhd,
            keylen,     /* target_depth */
            keylen,     /* len_query (not used) */
            targets,    /* stack */
            trieiter_hammingpairs_next,
            true        /* is_dirty */
            );
    
    return it;
}
