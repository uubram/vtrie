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
#include <Python.h>
#include "structmember.h"
#include "trie.h"

#if PY_MAJOR_VERSION >= 3
#define IS_PY3K
#endif

#ifdef IS_PY3K
#define PyString_Check PyBytes_Check
#define PyString_AsString PyBytes_AsString
#define PyString_FromString PyUnicode_FromString
#define PyInt_FromLong PyLong_FromLong

void
PyString_Concat(PyObject **string, PyObject *newpart)
{
    PyObject *tmp = *string;
    *string = PyUnicode_Concat(*string, newpart);
    Py_XDECREF(tmp);
}
#endif

typedef struct PyTrie PyTrie;
typedef struct PyTrieIter PyTrieIter;
typedef PyObject * (*PyTrieIterNextFunc)(PyTrieIter *);

/*
 * Py_DECREF is a macro, so wrap it so it can be passed around as a function.
 */
static void
Py_dealloc(void *py_obj)
{
    Py_DECREF((PyObject *) py_obj);
}

/* Return 0 in case of no error, else -1 */
static int 
Py_check_trieiter(TrieIter *it)
{
    const char *msg;
    int errcode = trieiter_errcode(it);
    switch(errcode){
    case E_SUCCESS:
        return 0;
    case E_OUT_OF_SYNC:
        msg = "(errcode: %d) Trie structure modified since iterator creation";
        break;
    case E_REPLACED:
        msg = "(errcode: %d) Replaced by another dirty iterator";
        break;
    default:
        msg = "(errcode: %d) Unknown error";
    }
    PyErr_Format(PyExc_RuntimeError, msg, errcode);
    return -1;
}

/*****************************************************************************
 * Trie iterator type                                                        *
 *****************************************************************************/

struct PyTrieIter {
    PyObject_HEAD
    PyTrie *trie;
    TrieIter *it;
    PyTrieIterNextFunc next;
};

static int
PyTrieIter_traverse(PyTrieIter *self, visitproc visit, void *arg)
{
    Py_VISIT(self->trie);
    return 0;
}

static int
PyTrieIter_clear(PyTrieIter *self)
{
    PyObject *tmp = (PyObject *)self->trie;
    self->trie = NULL;
    Py_XDECREF(tmp);
    return 0;
}

static void
PyTrieIter_dealloc(PyTrieIter *self)
{
    PyObject_GC_UnTrack(self);
    trieiter_free(self->it);
    PyTrieIter_clear(self);
    PyObject_GC_Del(self);
}

static PyTypeObject PyTrieIterType;

static PyObject *
PyTrieIter_new(PyTrie *trie, TrieIter *it, PyTrieIterNextFunc next)
{
    PyTrieIter *py_it = PyObject_GC_New(PyTrieIter, &PyTrieIterType);
    if (py_it == NULL)
        return NULL;

    /* Keep a reference to the trie to prevent the iterator from potentially
     * trying to iterate over a garbage collected PyTrie. */
    Py_INCREF(trie);
    py_it->trie = trie;

    py_it->it = it;
    py_it->next = next;

    PyObject_GC_Track(py_it);
    return (PyObject *)py_it;
}

static PyObject *
PyTrieIter_next(PyTrieIter *self)
{
    PyObject *result = self->next(self);
    if (Py_check_trieiter(self->it) != 0)
        return NULL;
    return result;
}

static PyTypeObject PyTrieIterType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "vtrie.PyTrieIter",                         /* tp_name */
    sizeof(PyTrieIter),                         /* tp_basicsize */
    0,                                          /* tp_itemsize */
    (destructor)PyTrieIter_dealloc,             /* tp_dealloc */
    0,                                          /* tp_print */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_compare */
    0,                                          /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    0,                                          /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,    /* tp_flags */
    0,                                          /* tp_doc */
    (traverseproc)PyTrieIter_traverse,          /* tp_traverse */
    (inquiry)PyTrieIter_clear,                  /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    PyObject_SelfIter,                          /* tp_iter */
    (iternextfunc)PyTrieIter_next,              /* tp_iternext */
    0,                                          /* tp_methods */
    0,                                          /* tp_members */
    0,                                          /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    0,                                          /* tp_init */
    0,                                          /* tp_alloc */
    0,                                          /* tp_new */
    0,                                          /* tp_free */
    0,                                          /* tp_is_gc */
    0,                                          /* tp_bases */
    0,                                          /* tp_mro */
    0,                                          /* tp_cache */
    0,                                          /* tp_subclasses */
    0,                                          /* tp_weaklist */
    0,                                          /* tp_del */
    0,                                          /* tp_version_tag */
};

/*****************************************************************************
 * Trie type                                                                 *
 *****************************************************************************/

struct PyTrie{
    PyObject_HEAD
    TrieRoot *root;
};

static PyObject *
PyTrie_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    (void)args;
    (void)kwds;
    PyTrie *self = NULL;

    if (type != NULL && type->tp_alloc != NULL){
        /* the PyType_GenericAlloc already turns on GC tracking, so no need to
         * call PyObject_GC_Track */
        self = (PyTrie *)type->tp_alloc(type, 0);
        if (self != NULL)
            self->root = trie_new();
    }

    return (PyObject *)self;
}

static int
PyTrie_init(PyTrie *self, PyObject *args, PyObject *kwds)
{
    (void)kwds;
    PyObject *other = NULL;

    if (!PyArg_UnpackTuple(args, "Trie", 0, 1, &other))
        return -1;

    if (other != NULL){
        if (PyTuple_Check(other) != 0){
            Py_ssize_t size = PyTuple_GET_SIZE(other);
            for (Py_ssize_t i = 0; i < size; i++){
                PyObject *item = PyTuple_GET_ITEM(other, i);
                if (PyTuple_Check(item) == 0 || PyTuple_GET_SIZE(item) != 2)
                    return -1;
                PyObject *key = PyTuple_GET_ITEM(item, 0);
                PyObject *value = PyTuple_GET_ITEM(item, 1);
                const char *s = PyString_AsString(key);
                if (PyErr_Occurred() != NULL)
                    return -1;
                Py_INCREF(value);
                trie_set_item(self->root, s, value, Py_dealloc);
            }
        }
    }

    return 0;
}

static int
PyTrie_traverse(PyTrie *self, visitproc visit, void *arg)
{
    TrieIter *it = trieiter_suffixes(self->root, "");
    TrieSearchResult *sr = NULL;
    while((sr = trieiter_next(it)) != NULL){
        if (sr != NULL && sr->target != NULL)
            Py_VISIT(sr->target->value);
        free(sr);
    }
    return 0;
}

static int
PyTrie_clear(PyTrie *self)
{
    trie_free(self->root, Py_dealloc);
    self->root = NULL;
    return 0;
}

static void
PyTrie_dealloc(PyTrie *self)
{
    PyObject_GC_UnTrack(self);
    PyTrie_clear(self);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* Return 1 if `key` is in trie `self`, 0 if not, and -1 on error. */
int
PyTrie_sq_contains(PyTrie *self, PyObject *key)
{
    if (!PyString_Check(key)){
        PyErr_SetString(PyExc_TypeError, "key is not a string");
        return -1;
    }
    if (trie_has_key(self->root, PyString_AsString(key)))
        return 1;
    else
        return 0;
}

static PyObject *
PyTrie_contains(PyTrie *self, PyObject *key)
{
    if (!PyString_Check(key)){
        PyErr_SetString(PyExc_TypeError, "key is not a string");
        return NULL;
    }
    if (trie_has_key(self->root, PyString_AsString(key)))
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}

static PyObject *
PyTrie_get(PyTrie *self, PyObject *args)
{
    PyObject *key;
    PyObject *failobj = Py_None;
    PyObject *val = NULL;

    if (!PyArg_UnpackTuple(args, "get", 1, 2, &key, &failobj))
        return NULL;

    char *s = PyString_AsString(key);
    if (PyErr_Occurred() != NULL)
        return NULL;

    const TrieItem *item = trie_get_item(self->root, s);

    if (item == NULL || item->value == NULL)
        val = failobj;
    else
        val = item->value;

    Py_INCREF(val);
    return val;
}

static PyObject *
PyTrie_setdefault(PyTrie *self, PyObject *args)
{
    PyObject *key;
    PyObject *failobj = Py_None;
    PyObject *val;

    if (!PyArg_UnpackTuple(args, "setdefault", 1, 2, &key, &failobj))
        return NULL;

    char *s = PyString_AsString(key);
    if (PyErr_Occurred() != NULL)
        return NULL;

    const TrieItem *item = trie_get_item(self->root, s);

    if (item == NULL || item->key == NULL){
        /* Adding failobj to the trie, so take ownership of a reference */
        Py_INCREF(failobj);
        trie_set_item(self->root, s, failobj, Py_dealloc);
        val = failobj;
    }else{
        val = item->value;
    }
    Py_INCREF(val);
    return val;
}

static PyObject *
PyTrie_pop(PyTrie *self, PyObject *args)
{
    PyObject *old_value;
    PyObject *key, *deflt = NULL;

    if (!PyArg_UnpackTuple(args, "pop", 1, 2, &key, &deflt))
        return NULL;

    if (trie_num_items(self->root) == 0){
        if (deflt){
            Py_INCREF(deflt);
            return deflt;
        }
        PyErr_SetString(PyExc_KeyError, "pop(): trie is empty");
        return NULL;
    }
    const char *s = PyString_AsString(key);
    if (PyErr_Occurred() != NULL)
        return NULL;

    const TrieItem *item = trie_get_item(self->root, s);
    if (item == NULL){
        if (deflt){
            Py_INCREF(deflt);
            return deflt;
        }
        PyErr_SetObject(PyExc_KeyError, key);
        return NULL;
    }
    old_value = item->value;
    /* Do not pass Py_dealloc to the trie_del_item function here, so that
     * the reference owned by PyTrie is passed to the caller of pop(). */
    if (trie_del_item(self->root, s, NULL) != 0){
        PyErr_SetString(PyExc_RuntimeError, "Unable to delete item");
        return NULL;
    }
    return old_value;
}

static PyObject *
PyTrie_popitem(PyTrie *self)
{
    if (trie_num_items(self->root) == 0){
        PyErr_SetString(PyExc_KeyError, "popitem(): trie is empty");
        return NULL;
    }

    TrieIter *it = trieiter_suffixes(self->root, "");
    if (it == NULL){
        PyErr_SetString(PyExc_RuntimeError, "Failed to create iterator");
        return NULL;
    }

    TrieSearchResult *sr = trieiter_next(it);
    
    if (Py_check_trieiter(it) != 0)
        return NULL;

    if (sr == NULL){
        PyErr_SetString(PyExc_RuntimeError, "Nothing found in non-empty trie");
        return NULL;
    }

    PyObject *key = PyString_FromString(sr->target->key);
    PyObject *value = sr->target->value;
    /* Do not pass Py_dealloc to the trie_del_item function here, so that
     * the reference owned by PyTrie is passed to the caller of popitem(). */
    if (trie_del_item(self->root, sr->target->key, NULL) != 0){
        PyErr_SetString(PyExc_RuntimeError, "Unable to delete item");
        free(sr);
        return NULL;
    }
    free(sr);
    return PyTuple_Pack(2, key, value);
}

static PyObject *
PyTrie_keys(PyTrie *self)
{
    size_t n = trie_num_items(self->root);
    PyObject *result = PyList_New(n);
    if (result == NULL)
        return NULL;
    TrieIter *it = trieiter_suffixes(self->root, "");
    if (it == NULL)
        return NULL;
    TrieSearchResult *sr = NULL;
    for (size_t i = 0; i < n; i++){
        sr = trieiter_next(it);
        if (Py_check_trieiter(it) != 0)
            return NULL;
        PyObject *key = PyString_FromString(sr->target->key);
        PyList_SET_ITEM(result, i, key);
    }
    return result;
}

static PyObject *
PyTrie_items(PyTrie *self)
{
    size_t n = trie_num_items(self->root);
    PyObject *result = PyList_New(n);
    if (result == NULL)
        return NULL;
    TrieIter *it = trieiter_suffixes(self->root, "");
    if (it == NULL)
        return NULL;
    TrieSearchResult *sr = NULL;
    for (size_t i = 0; i < n; i++){
        sr = trieiter_next(it);
        if (Py_check_trieiter(it) != 0)
            return NULL;
        PyObject *key = PyString_FromString(sr->target->key);
        PyObject *value = sr->target->value;
        Py_INCREF(value);
        PyList_SET_ITEM(result, i, PyTuple_Pack(2, key, value));
    }
    return result;
}

static PyObject *
PyTrie_values(PyTrie *self)
{
    size_t n = trie_num_items(self->root);
    PyObject *result = PyList_New(n);
    if (result == NULL)
        return NULL;
    TrieIter *it = trieiter_suffixes(self->root, "");
    if (it == NULL)
        return NULL;
    TrieSearchResult *sr = NULL;
    for (size_t i = 0; i < n; i++){
        sr = trieiter_next(it);
        if (Py_check_trieiter(it) != 0)
            return NULL;
        PyObject *value = sr->target->value;
        Py_INCREF(value);
        PyList_SET_ITEM(result, i, value);
    }
    return result;
}

static PyObject *
_PyTrieIter_keys_next(PyTrieIter *py_it)
{
    TrieSearchResult *result = trieiter_next(py_it->it);
    if (result == NULL)
        return NULL;

    PyObject *result_pyobj = PyString_FromString(result->target->key);
    free(result);
    return(result_pyobj);
}

static PyObject *
PyTrie_iterkeys(PyTrie *self)
{
    TrieIter *it = trieiter_suffixes(self->root, "");

    if (it == NULL){
        PyErr_SetString(PyExc_Exception, "Unable to get iterator");
        return NULL;
    }

    return PyTrieIter_new(self, it, _PyTrieIter_keys_next);
}

static PyObject *
_PyTrieIter_values_next(PyTrieIter *py_it)
{
    TrieSearchResult *result = trieiter_next(py_it->it);
    if (result == NULL)
        return NULL;

    PyObject *value = result->target->value;
    Py_INCREF(value);
    return value;
}

static PyObject *
PyTrie_itervalues(PyTrie *self)
{
    TrieIter *it = trieiter_suffixes(self->root, "");

    if (it == NULL){
        PyErr_SetString(PyExc_Exception, "Unable to get iterator");
        return NULL;
    }

    return PyTrieIter_new(self, it, _PyTrieIter_values_next);
}

static PyObject *
_PyTrie_items_next(PyTrieIter *py_it)
{
    TrieSearchResult *result = trieiter_next(py_it->it);
    if (result == NULL)
        return NULL;

    PyObject *key = PyString_FromString(result->target->key);
    PyObject *value = result->target->value;
    Py_INCREF(value);
    return PyTuple_Pack(2, key, value);
}

static PyObject *
PyTrie_iteritems(PyTrie *self)
{
    TrieIter *it = trieiter_suffixes(self->root, "");

    if (it == NULL){
        PyErr_SetString(PyExc_Exception, "Unable to get iterator");
        return NULL;
    }

    return PyTrieIter_new(self, it, _PyTrie_items_next);
}

static PyObject *
PyTrie_num_nodes(PyTrie *self)
{
    return PyInt_FromLong(trie_num_nodes(self->root));
}

static PyObject *
PyTrie_has_node(PyTrie *self, PyObject *args)
{
    PyObject *key = NULL;

    if (!PyArg_UnpackTuple(args, "has_node", 1, 1, &key))
       return NULL;

    const char *s = PyString_AsString(key);
    if (PyErr_Occurred() != NULL)
        return NULL;

    if (trie_has_node(self->root, s))
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}

static PyObject *
PyTrie_longest_prefix(PyTrie *self, PyObject *args, PyObject *kwds)
{
    const char *key;
    static char *kwlist[] = {"key", NULL};

#ifdef IS_PY3K
    const char format[] = "y";
#else
    const char format[] = "s";
#endif

    if (!PyArg_ParseTupleAndKeywords(args, kwds, format, kwlist, &key))
        return NULL;

    const TrieItem *item = trie_longest_prefix(self->root, key);

    if (item == NULL){
        Py_RETURN_NONE;
    }else{
        PyObject *key = PyString_FromString(item->key);
        PyObject *value = item->value;
        Py_INCREF(value);
        return PyTuple_Pack(2, key, value);
    }
}

static Py_ssize_t
PyTrie_length(PyTrie *self)
{
    return trie_num_items(self->root);
}

static PyObject *
PyTrie_sizeof(PyTrie *self)
{
    return PyInt_FromLong(trie_mem_usage(self->root));
}

/* GetItem function */
static PyObject *
PyTrie_subscript(PyTrie *self, PyObject *key)
{
    const char *s = PyString_AsString(key);
    if (PyErr_Occurred() != NULL)
        return NULL;

    const TrieItem *item = trie_get_item(self->root, s);

    if (item == NULL){
        PyErr_SetObject(PyExc_KeyError, key);
        return NULL;
    }

    Py_INCREF(item->value);
    return item->value;
}

/* SetItem and DelItem functions.
 * Return 0 on success and -1 on error. */
static int 
PyTrie_ass_subscript(PyTrie *self, PyObject *key, PyObject *value)
{
    const char *s = PyString_AsString(key);
    if (PyErr_Occurred() != NULL)
        return -1; 

    if (value == NULL){
        if (trie_del_item(self->root, s, Py_dealloc) != 0)
            return -1;
    }else{
        /* Take ownership of a reference to value, because Py_dealloc
         * might otherwise cause the value object to be freed in case it 
         * already exists in the trie. */
        Py_INCREF(value);
        if(trie_set_item(self->root, s, value, Py_dealloc) != 0){
            PyErr_SetString(PyExc_Exception,
                    "Unable to set value for string");
            Py_XDECREF(value);
            return -1;
        }
    }
    return 0;
}

static PyObject *
PyTrie_repr(PyTrie *self)
{
    PyObject *colon = NULL;
    PyObject *pieces = NULL;
    PyObject *result = NULL;
    TrieIter *it = NULL;

    Py_ssize_t i = Py_ReprEnter((PyObject *)self);
    if (i != 0){
        return i > 0? PyString_FromString("Trie{...}") : NULL;
    }

    if (trie_num_items(self->root) == 0){
        result = PyString_FromString("Trie{}");
        goto Done;
    }

    it = trieiter_suffixes(self->root, "");
    if (it == NULL){
        goto Done;
    }

    pieces = PyList_New(0);
    if (pieces == NULL)
        goto Done;

    colon = PyString_FromString(": ");
    if (colon == NULL)
        goto Done;

    /* Do repr() on each key:value pair and insert ": " between them. */
    PyObject *s = NULL;
    PyObject *temp = NULL;
    TrieSearchResult *sr = NULL;
    while ((sr = trieiter_next(it)) != NULL){
        PyObject *value = (PyObject *)sr->target->value;
        Py_INCREF(value);

        /* create 'key' string */
        temp = PyString_FromString(sr->target->key);
        s = PyObject_Repr(temp);
        Py_DECREF(temp);

        free(sr);

        /* create 'key: value' string */
        PyString_Concat(&s, colon);
        temp = PyObject_Repr(value);
        PyString_Concat(&s, temp);
        Py_DECREF(temp);
        Py_DECREF(value);
        if (s == NULL)
            goto Done;

        int status = PyList_Append(pieces, s);
        Py_DECREF(s); /* append created a new ref */
        if (status < 0)
            goto Done;
    }

    if (Py_check_trieiter(it) != 0)
        goto Done;

    s = PyString_FromString("Trie{");
    if (s == NULL)
        goto Done;
    temp = PyList_GET_ITEM(pieces, 0);
    PyString_Concat(&s, temp);
    Py_DECREF(temp);
    PyList_SET_ITEM(pieces, 0, s);
    if (s == NULL)
        goto Done;

    s = PyString_FromString("}");
    if (s == NULL)
        goto Done;
    temp = PyList_GET_ITEM(pieces, PyList_GET_SIZE(pieces)-1); 
    PyString_Concat(&temp, s);
    Py_DECREF(s);
    PyList_SET_ITEM(pieces, PyList_GET_SIZE(pieces)-1, temp);

    s = PyString_FromString(", ");
    if (s == NULL)
        goto Done;
    result = PyObject_CallMethod(s, "join", "O", pieces);
    Py_DECREF(s);
Done:
    trieiter_free(it);
    Py_XDECREF(pieces);
    Py_XDECREF(colon);
    Py_ReprLeave((PyObject *)self);
    return result;
}

static PyObject *
_PyTrieIter_suffixes_next(PyTrieIter *py_it)
{
    TrieSearchResult *result = trieiter_next(py_it->it);
    if (result == NULL)
        return NULL;

    PyObject *result_pyobj = Py_BuildValue("(sO)",
            result->target->key + trieiter_len_query(py_it->it),
            result->target->value);
    free(result);
    return(result_pyobj);
}

static PyObject *
PyTrie_suffixes(PyTrie *self, PyObject *args)
{
    PyObject *key = NULL;

    if (!PyArg_UnpackTuple(args, "suffixes", 1, 1, &key))
        return NULL;

    const char *s = PyString_AsString(key);
    if (PyErr_Occurred() != NULL)
        return NULL;

    TrieIter *it = trieiter_suffixes(self->root, s);

    if (it == NULL){
        PyErr_SetString(PyExc_Exception, "Unable to get iterator");
        return NULL;
    }

    return PyTrieIter_new(self, it, _PyTrieIter_suffixes_next);
}

static PyObject *
_PyTrieIter_neighbors_next(PyTrieIter *py_it)
{
    TrieSearchResult *result = trieiter_next(py_it->it);
    if (result == NULL)
        return NULL;

    PyObject *result_pyobj = Py_BuildValue("(isO)",
            result->hd, result->target->key, result->target->value);
    free(result);
    return(result_pyobj);
}

static PyObject *
PyTrie_neighbors(PyTrie *self, PyObject *args, PyObject *kwds)
{
    char *s;
    int maxhd;
    static char *kwlist[] = {"s", "maxhd", NULL};

#ifdef IS_PY3K
    const char *format = "yi";
#else
    const char *format = "si";
#endif

    if (!PyArg_ParseTupleAndKeywords(args, kwds, format, kwlist, &s, &maxhd))
        return NULL;

    if (maxhd < 1){
        PyErr_SetString(PyExc_ValueError, "maxhd < 1");
        return NULL;
    }

    TrieIter *it = trieiter_neighbors(self->root, s, maxhd);

    if (it == NULL){
        PyErr_SetString(PyExc_Exception,
                "Unable to get iterator (key does not exist?)");
        return NULL;
    }

    return PyTrieIter_new(self, it, _PyTrieIter_neighbors_next);
}

static PyObject *
_PyTrieIter_pairs_next(PyTrieIter *py_it)
{
    TrieSearchResult *result = trieiter_next(py_it->it);
    if (result == NULL)
        return NULL;

    PyObject *result_pyobj = Py_BuildValue("(isOsO)",
            result->hd,
            result->query->key, result->query->value,
            result->target->key, result->target->value);
    free(result);
    return result_pyobj;
}

static PyObject *
PyTrie_pairs(PyTrie *self, PyObject *args, PyObject *kwds)
{
    int keylen;
    int maxhd;
    static char *kwlist[] = {"keylen", "maxhd", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii", kwlist, &keylen,
                &maxhd))
        return NULL;

    if (keylen < 0){
        PyErr_SetString(PyExc_ValueError, "keylen < 1");
        return NULL;
    }

    if (maxhd < 1){
        PyErr_SetString(PyExc_ValueError, "maxhd < 1");
        return NULL;
    }

    TrieIter *it = trieiter_hammingpairs(self->root, keylen, maxhd);

    if (it == NULL){
        PyErr_SetString(PyExc_Exception, "Unable to get iterator");
        return NULL;
    }

    return PyTrieIter_new(self, it, _PyTrieIter_pairs_next);
}

/*
 * Creates a tuple of (key, value) pairs.
 */
static PyObject *
PyTrie_reduce(PyTrie *self)
{
    if (self->root == NULL)
        return NULL;
    TrieIter *it = trieiter_suffixes(self->root, "");
    if (it == NULL)
        return NULL;

    size_t n = trie_num_items(self->root);
    PyObject *arg = PyTuple_New(n);

    if (arg == NULL)
        return NULL;

    for (size_t i = 0; i < n; i++){
        TrieSearchResult *sr = trieiter_next(it);
        if (sr == NULL || sr->target == NULL || sr->target->key == NULL ||
                sr->target->value == NULL)
            goto fail;
#ifdef IS_PY3K
        PyObject *key = PyBytes_FromString(sr->target->key);
#else
        PyObject *key = PyString_FromString(sr->target->key);
#endif
        if (PyErr_Occurred() != NULL)
            goto fail;
        PyObject *value = sr->target->value;
        Py_INCREF(value);
        PyTuple_SET_ITEM(arg, i, PyTuple_Pack(2, key, value));
    }
    return PyTuple_Pack(2, Py_TYPE(self), PyTuple_Pack(1,arg));
fail:
    Py_DECREF(arg);
    return NULL;
}

PyDoc_STRVAR(reduce__doc__,
"T.__reduce__() -> tuple containing all (key, value) pairs from the trie as \n\
2-tuples.");

PyDoc_STRVAR(contains__doc__,
"T.__contains__(k) -> True if T has a key k, else False");

PyDoc_STRVAR(getitem__doc__,
"x.__getitem__(y) <==> x[y]");

PyDoc_STRVAR(sizeof__doc__,
"T.__sizeof__() -> size of T in memory, in bytes");

PyDoc_STRVAR(has_key__doc__,
"T.has_key(k) -> True if T has a key k, else False");

PyDoc_STRVAR(get__doc__,
"T.get(k[,d]) -> T[k] if k in T, else d. d defaults to None.");

PyDoc_STRVAR(setdefault__doc__,
"T.setdefault(k[,d]) -> T.get(k,d), also set T[k]=d if k not in T");

PyDoc_STRVAR(pop__doc__,
"T.pop(k[,d]) -> v, remove specified key and return the corresponding value.\n\
If key is not found, d is returned if given, otherwise KeyError is raised.");

PyDoc_STRVAR(popitem__doc__,
"T.popitem() -> (k, v), remove and return some (key, value) pair as a\n\
2-tuple; but raise KeyError if T is empty.");

PyDoc_STRVAR(keys__doc__,
"T.keys() -> list of T's keys");

PyDoc_STRVAR(items__doc__,
"T.items() -> list of T's (key, value) pairs, as 2-tuples");

PyDoc_STRVAR(values__doc__,
"T.values() -> list of T's values");

PyDoc_STRVAR(iterkeys__doc__,
"T.iterkeys() -> an iterator over the keys of T");

PyDoc_STRVAR(itervalues__doc__,
"T.itervalues() -> an iterator over the values of T");

PyDoc_STRVAR(iteritems__doc__,
"T.iteritems() -> an iterator over the items of T");


/* Trie-specific functionality */

PyDoc_STRVAR(num_nodes__doc__,
"T.num_nodes() -> number of nodes in T");

PyDoc_STRVAR(has_node__doc__,
"T.has_node(k) -> True if T has a node corresponding to T[k], even if k is \n\
not a key in T, else False.");

PyDoc_STRVAR(longest_prefix__doc__,
"T.longest_prefix(k) -> find longest key matching the beginning of k, \n\
returning (key, value) pair as a 2-tuple. None is returned if no match.");

PyDoc_STRVAR(suffixes__doc__,
"T.suffixes(k) -> iterate over all (suffix, value) pairs in T, as 2-tuples, \n\
that have k as a prefix.");

PyDoc_STRVAR(neighbors__doc__,
"T.neighbors(key=k, maxhd=n) -> iterate over all \n\
(Hamming distance, key, value) triples, as 3-tuples,\n\
where key and k differ by at least 1, but maximally n characters.");

PyDoc_STRVAR(pairs__doc__,
"T.pairs(keylen=l, maxhd=n) -> iterate over *ALL* \n\
(Hamming distance, key1, value1, key2, value2) 5-tuples, \n\
where key1 and key2 differ by at least 1, but maximally n characters.");

static PyMethodDef PyTrie_methods[] = {
    {"__reduce__",      (PyCFunction)PyTrie_reduce, METH_NOARGS,
        reduce__doc__},
    {"__contains__",    (PyCFunction)PyTrie_contains, METH_O | METH_COEXIST,
        contains__doc__},
    {"__getitem__",     (PyCFunction)PyTrie_subscript, METH_O | METH_COEXIST,
        getitem__doc__},
    {"__sizeof__",      (PyCFunction)PyTrie_sizeof, METH_NOARGS,
        sizeof__doc__},
    {"has_key",         (PyCFunction)PyTrie_contains, METH_O | METH_COEXIST,
        has_key__doc__},
    {"get",             (PyCFunction)PyTrie_get, METH_VARARGS,
        get__doc__},
    {"setdefault",      (PyCFunction)PyTrie_setdefault, METH_VARARGS,
        setdefault__doc__},
    {"pop",             (PyCFunction)PyTrie_pop, METH_VARARGS,
        pop__doc__},
    {"popitem",         (PyCFunction)PyTrie_popitem, METH_NOARGS,
        popitem__doc__},
    {"keys",            (PyCFunction)PyTrie_keys, METH_NOARGS,
        keys__doc__},
    {"items",           (PyCFunction)PyTrie_items, METH_NOARGS,
        items__doc__},
    {"values",          (PyCFunction)PyTrie_values, METH_NOARGS,
        values__doc__},
    {"iterkeys",        (PyCFunction)PyTrie_iterkeys, METH_NOARGS,
        iterkeys__doc__},
    {"itervalues",      (PyCFunction)PyTrie_itervalues, METH_NOARGS,
        itervalues__doc__},
    {"iteritems",       (PyCFunction)PyTrie_iteritems, METH_NOARGS,
        iteritems__doc__},

    /* Trie specific methods */
    {"num_nodes",       (PyCFunction)PyTrie_num_nodes, METH_NOARGS,
        num_nodes__doc__},
    {"has_node",        (PyCFunction)PyTrie_has_node, METH_VARARGS,
        has_node__doc__},
    {"longest_prefix",  (PyCFunction)PyTrie_longest_prefix,
        METH_VARARGS | METH_KEYWORDS, longest_prefix__doc__},
    {"suffixes",        (PyCFunction)PyTrie_suffixes,
        METH_VARARGS, suffixes__doc__},
    {"neighbors",       (PyCFunction)PyTrie_neighbors,
        METH_VARARGS | METH_KEYWORDS, neighbors__doc__},
    {"pairs",           (PyCFunction)PyTrie_pairs,
        METH_VARARGS | METH_KEYWORDS, pairs__doc__},
    {NULL, NULL, 0, NULL} /* Sentinel */
};

/* Hack to implement "key in vtrie" */
static PySequenceMethods PyTrie_as_sequence = {
    0,                                  /* sq_length */
    0,                                  /* sq_concat */
    0,                                  /* sq_repeat */
    0,                                  /* sq_item */
    0,                                  /* sq_slice */
    0,                                  /* sq_ass_item */
    0,                                  /* sq_ass_slice */
    (objobjproc)PyTrie_sq_contains,     /* sq_contains */
    0,                                  /* sq_inplace_concat */
    0,                                  /* sq_inplace_repeat */
};

static PyMappingMethods PyTrie_as_mapping = {
    (lenfunc)PyTrie_length,               /* mp_length */
    (binaryfunc)PyTrie_subscript,         /* mp_subscript */
    (objobjargproc)PyTrie_ass_subscript   /* mp_ass_subscript */
};

static PyMemberDef PyTrie_members[] = {
    {NULL, 0, 0, 0, ""} /* Sentinel */
};

PyDoc_STRVAR(trie_doc, "Trie() -> new empty trie");

static PyTypeObject PyTrieType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "vtrie.Trie",                               /* tp_name */
    sizeof(PyTrie),                             /* tp_basicsize */
    0,                                          /* tp_itemsize */
    (destructor)PyTrie_dealloc,                 /* tp_dealloc */
    0,                                          /* tp_print */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_compare */
    (reprfunc)PyTrie_repr,                      /* tp_repr */
    0,                                          /* tp_as_number */
    &PyTrie_as_sequence,                        /* tp_as_sequence */
    &PyTrie_as_mapping,                         /* tp_as_mapping */
    PyObject_HashNotImplemented,                /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    0,                                          /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,    /* tp_flags */
    trie_doc,                                   /* tp_doc */
    (traverseproc)PyTrie_traverse,              /* tp_traverse */
    (inquiry)PyTrie_clear,                      /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    (getiterfunc)PyTrie_iterkeys,               /* tp_iter */
    0,                                          /* tp_iternext */
    PyTrie_methods,                             /* tp_methods */
    PyTrie_members,                             /* tp_members */
    0,                                          /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    (initproc)PyTrie_init,                      /* tp_init */
    PyType_GenericAlloc,                        /* tp_alloc */
    PyTrie_new,                                 /* tp_new */
    PyObject_GC_Del,                            /* tp_free */
    0,                                          /* tp_is_gc */
    0,                                          /* tp_bases */
    0,                                          /* tp_mro */
    0,                                          /* tp_cache */
    0,                                          /* tp_subclasses */
    0,                                          /* tp_weaklist */
    0,                                          /* tp_del */
    0,                                          /* tp_version_tag */
};

#ifdef IS_PY3K
static PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "vtrie",
    "",
    -1,
    NULL, NULL, NULL, NULL, NULL
};

#else

static PyMethodDef methods[] = {
    {NULL, NULL, 0, NULL}, /* Sentinel */
};

#endif

/*
 * Initialize module for either Python 2 or 3+.
 */
#ifdef IS_PY3K

#define INITERROR return NULL

PyObject *
PyInit_vtrie(void)

#else

#define INITERROR return

#ifndef PyMODINIT_FUNC /* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif

PyMODINIT_FUNC
initvtrie(void)
#endif
{
    PyObject* m;
    PyObject *op;

    if (PyType_Ready(&PyTrieType) < 0)
        INITERROR;

    if (PyType_Ready(&PyTrieIterType) < 0)
        INITERROR;

#ifdef IS_PY3K
    m = PyModule_Create(&moduledef);
#else
    m = Py_InitModule("vtrie", methods);
#endif

    if (m == NULL)
        INITERROR;

    op = (PyObject *)&PyTrieType;
    Py_INCREF(op);
    op = (PyObject *)&PyTrieIterType;
    Py_INCREF(op);
    PyModule_AddObject(m, "Trie", (PyObject *)&PyTrieType);
    PyModule_AddObject(m, "PyTrieIter", (PyObject *)&PyTrieIterType);

#ifdef IS_PY3K
    return m;
#endif
}
