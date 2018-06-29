vtrie
=====

Trie structure supporting approximate string matching (substitutions only) for
Python (2.x and 3.x).

.. NOTE::

        Only ascii strings are supported. Hence, Python 3 users should use
        'b' prefix to insert strings into the trie.

Installation
============

::

        pip install vtrie

Features
========

* It is similar to a dict() in general usage, and supports much of the dict()
  interface:

  * __len__(): number of items in the trie
  * __contains()__
  * __getitem()__
  * __setitem()__
  * __delitem()__
  * __sizeof()__: size of the trie in bytes
  * __repr()__: dict-like output, showing contents of the trie
  * has_key()
  * setdefault()
  * get()
  * pop()
  * popitem()
  * keys()
  * values()
  * items()
  * iterkeys()
  * itervalues()
  * iteritems()

* Trie-specific methods:

  * num_nodes(): number of nodes
  * longest_prefix(k): find longest key matching the beginning of k,
    returning (key, value) pair as a 2-tuple. None is returned if no match.
  * suffixes(k): iterate over all (suffix, value) pairs as 2-tuples, that
    have k as a prefix.
  * neighbors(key = k, maxhd = n): iterate over all
    (Hamming distance, key, value) triples, as 3 tuples, where key and k
    differ by at least 1, but maximally n characters.
    Note, one can only search for neighbors of *existing* keys.
  * pairs(keylen = l, maxhd = n): iterate over *ALL*
    (Hamming distance, key1, value1, key2, value2) 5-tuples where key1 and key2
    differ by at least 1, but maximally n characters. Note, pairs() returns a
    dirty iterator, meaning that nodes in the trie are modified while the
    iterator is running. An exception will be thrown when iterating with more
    than one dirty iterator.

* Pickling

Usage
=====

Create a trie::

        >>> from vtrie import Trie
        >>> t = Trie()

Add strings to the trie. Currently, only ascii strings are supported::

        >>> t[b"Hello"] = 123
        >>> t[b"World"] = {"my": "dict"}
        >>> t[b"foo"] = None

Check if "Hello" is in the trie::

        >>> b"Hello" in t
        True

Show all inserted strings sharing the same prefix::

        >>> t[b"foo"] = 0
        >>> t[b"foobar"] = 1
        >>> t[b"fooo"] = 2
        >>> t[b"hello"] = 3
        >>> list(t.suffixes(b"fo"))
        [('o', 0), ('obar', 1), ('oo', 2)]

Search for keys that differ by less than a given number of substitutions from
the provided key. The results are tuples with first the Hamming distance
between the given key and the found key, then the found key and its value::

        >>> t[b"hello world"] = 0
        >>> t[b"*ello world"] = "a"
        >>> t[b"*ell* world"] = "b"
        >>> t[b"*ell* w*rld"] = "c"
        >>> t[b"hell* w*rl*"] = "d"
        >>> list(t.neighbors(b"hello world", maxhd = 1))
        [(1, '*ello world', 'a')]
        >>> list(t.neighbors(b"hello world", maxhd = 2))
        [(1, '*ello world', 'a'), (2, '*ell* world', 'b')]
        >>> print("\n".join(map(str,list(t.neighbors(b"hello world", 3)))))
        (3, 'hell* w*rl*', 'd')
        (1, '*ello world', 'a')
        (2, '*ell* world', 'b')
        (3, '*ell* w*rld', 'c')

Search for all keys of a certain length that are within a certain Hamming of
each other. The results are tuples with first the Hamming distance between the
found keys, then the first key and its value, and then the second key and
its value::

        >>> print("\n".join(map(str,list(t.pairs(keylen = 11, maxhd = 2)))))
        (1, 'hello world', 0, '*ello world', 'a')
        (2, 'hello world', 0, '*ell* world', 'b')
        (2, 'hell* w*rl*', 'd', '*ell* w*rld', 'c')
        (1, '*ello world', 'a', '*ell* world', 'b')
        (2, '*ello world', 'a', '*ell* w*rld', 'c')
        (1, '*ell* world', 'b', '*ell* w*rld', 'c')
