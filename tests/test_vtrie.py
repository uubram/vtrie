import sys
if sys.version_info < (3,):
    def b(x):
        return x
else:
    import codecs
    xrange = range
    def b(x):
        return codecs.latin_1_encode(x)[0]

import pytest
import pickle
from itertools import product, permutations
from vtrie import Trie

def test_reduce():
    t = Trie()
    t[b"hello"] = 1
    t[b"world"] = [1,2,3]
    for i in xrange(100):
        t[b(str(i))] = i
    t2 = pickle.loads(pickle.dumps(t))
    assert len(t) == len(t2)
    assert t.__sizeof__() == t2.__sizeof__()
    for e in t:
        assert t[b(e)] == t2[b(e)]

def test_len():
    t = Trie()
    assert len(t) == 0
    t[b"Hello"] = 123
    assert len(t) == 1
    t[b"World"] = "!"
    assert len(t) == 2
    t[b"Hello"] = 0
    assert len(t) == 2
    del t[b"World"]
    assert len(t) == 1
    del t[b"Hello"]
    assert len(t) == 0

def test_contains():
    t = Trie()
    assert not b"abc" in t
    t[b"abc"] = 1
    assert b"abc" in t
    del t[b"abc"]
    assert not b"abc" in t
    with pytest.raises(TypeError):
        5 in t

    t = Trie()
    assert not t.__contains__(b"abc")
    t[b"abc"] = 1
    assert t.__contains__(b"abc")
    del t[b"abc"]
    assert not t.__contains__(b"abc")
    with pytest.raises(TypeError):
        t.__contains__(5)

def test_getitem_setitem():
    t = Trie()
    t[b"Hello"] = "world"
    assert t[b"Hello"] == "world"
    # Test getting a non-inserted string raises an exception
    with pytest.raises(KeyError):
        t[b"Hello!"]
    with pytest.raises(KeyError):
        t[b"He"]
    with pytest.raises(KeyError):
        t[b""]

    # Test associating strings with a value, it should be possible to associate
    # strings with None as well.
    t = Trie()
    t[b"Hello"] = 0
    assert t[b"Hello"] == 0
    t[b"world"] =  "!"
    assert t[b"world"] == "!"
    t[b"ABC"] = (1,2,3,4)
    assert t[b"ABC"] == (1,2,3,4)
    t[b"AAA"] = None
    assert t[b"AAA"] is None

    # Test that the associated value is not a copy
    val = [1,2,3]
    t[b"XYZ"] = val
    assert t[b"XYZ"] is val 
    val[1] = 9
    assert t[b"XYZ"] == [1,9,3]

    # If setitem does not make the trie own a reference to the inserted value,
    # the following may result in a memory corruption error.
    t = Trie()
    t[b"a"] = []
    for i in xrange(100):
        t[b"a"].append(1)

    # Test that order of insertions does not affect string associations
    strvals = [
        (b"Hello", 0),
        (b"world", "!"),
        (b"there", (1,2,3)),
        (b"abcde", None),
        (b"dicti", {"a": None, "dict": (1,2,3)})]
    for perm in permutations(strvals):
        t = Trie()
        for (s, val) in perm:
            t[s] = val
        for (s, val) in strvals:
            assert t[s] is val

    # Test staggered insertions
    t = Trie()
    t[b"A"] = 0
    t[b"AB"] = (1,2)
    t[b"ABC"] = ["..."]
    assert t[b"A"] == 0
    assert t[b"AB"] == (1,2)
    assert t[b"ABC"] == ["..."]

    # Test inserting empty string works
    t = Trie()
    t[b""] = 123
    assert t[b""] == 123

    # Test overwriting values
    t[b"myval"] = 14
    assert t[b"myval"] == 14
    t[b"myval"] = [1,2,3,4]
    assert t[b"myval"] == [1,2,3,4]

    # Test that lists and tuples cannot be used in place of strings
    t = Trie()
    with pytest.raises(TypeError):
        t[ (1,2,3) ] = 0
    with pytest.raises(TypeError):
        t[ [1,2] ] = 0
    with pytest.raises(TypeError):
        t[None]

def test_delitem():
    t = Trie()
    t[b"hellothere"] = 0
    t[b"helloworld"] = 1
    assert t[b"hellothere"] == 0
    assert t[b"helloworld"] == 1
    assert t.has_node(b"hello")

    del t[b"hellothere"]
    with pytest.raises(KeyError):
        t[b"hellothere"]
    assert not t.has_node(b"hellothere")
    assert not t.has_node(b"hellot")
    assert t.has_node(b"hello")
    with pytest.raises(KeyError):
        t[b"hello"]
    assert t[b"helloworld"] == 1

    # Test removing strings with insertions staggered
    t = Trie()
    strings = [b"AB", b"ABCD", b"ABCDEFG", b"ABCDEFGHIJK"]
    for i, s in enumerate(strings):
        t[s] = i;
    assert t[b"AB"] == 0
    assert t[b"ABCDEFGHIJK"] == 3
    del t[b"AB"]
    assert t.has_node(b"AB")
    # Make sure string is removed
    with pytest.raises(KeyError):
        t[b"AB"]
    # Check the other strings were not affected
    for i, s in enumerate(strings[1:]):
        assert t[s] == i + 1
    # Should not be able to remove non-existent string
    assert t.has_node(b"ABC")
    with pytest.raises(Exception):
        del t[b"ABC"]
    with pytest.raises(Exception):
        del t[b"ABCDEFGHIJKL"]
    # Remove string in between two others
    del t[b"ABCDEFG"]
    assert t.has_node(b"ABCDEFG")
    with pytest.raises(KeyError):
        t[b"ABCDEFG"]
    assert t[b"ABCDEFGHIJK"] == 3
    # See if nodes are actually removed when a string at a leaf is removed
    del t[b"ABCDEFGHIJK"]
    assert t.has_node(b"ABCD")
    assert t[b"ABCD"] == 1
    assert not t.has_node(b"ABCDE")
    with pytest.raises(KeyError):
        t[b"ABCDEFG"]
    with pytest.raises(KeyError):
        t[b"ABCDEFGHIJK"]
    # Remove the last string
    del t[b"ABCD"]
    assert not t.has_node(b"ABCD")
    assert not t.has_node(b"A")
    assert t.has_node(b"")
    with pytest.raises(KeyError):
        t[b""]

    # Test edge case where empty string is inserted and removed
    with pytest.raises(KeyError):
        t[b""]
    t[b""] = 0
    assert t[b""] == 0
    del t[b""]
    assert t.has_node(b"")
    with pytest.raises(KeyError):
        t[b""]

def test_sizeof():
    # NOTE: this test may fail because it assumes to know the size in bytes of
    # the root node and of non-root nodes. So failure of this test may not
    # necessarily indicate that the trie is reporting the wrong size.
    t = Trie()
    rs = 96 # size of root node in bytes
    ns = 56 # size of trie node in bytes
    sizeof = lambda t:t.__sizeof__()
    assert t.__sizeof__() == sizeof(t) == rs
    t[b"a"] = 1
    assert sizeof(t) == rs + ns + 1 + 1
    del t[b"a"]
    assert sizeof(t) == rs
    t[b"hello"] = 1
    assert sizeof(t) == rs + ns*5 + 5 + 1
    t[b"world"] = 1
    assert sizeof(t) == rs + ns*10 + 10 + 2
    t[b"h3llo"] = 1
    assert sizeof(t) == rs + ns*14 + 15 + 3
    t[b"hello"] = 2 # modifying existing value should not change the size
    assert sizeof(t) == rs + ns*14 + 15 + 3
    t[b"here"] = 3 # should add 2 nodes
    assert sizeof(t) == rs + ns*16 + 19 + 4
    del t[b"hello"] # can only remove "llo" nodes
    assert sizeof(t) == rs + ns*13 + 14 + 3

def test_has_key():
    t = Trie()
    assert not t.has_key(b"a")
    t[b"a"] = 1
    assert t.has_key(b"a")
    del t[b"a"]
    assert not t.has_key(b"a")

def test_get():
    t = Trie()
    assert t.get(b"foo", 123) == 123
    assert not b"foo"in t
    t[b"foo"] = 1
    assert b"foo" in t
    assert t.get(b"foo", 123) == 1
    assert t.get(b"fo", 123) == 123
    assert t.get(b"fo") is None # should default to None

def test_setdefault():
    t = Trie()
    assert t.setdefault(b"a", 123) == 123
    assert t[b"a"] == 123
    assert t.setdefault(b"a", 5) == 123
    assert t[b"a"] == 123
    assert t.setdefault(b"a") == 123 
    assert t[b"a"] == 123
    assert t.setdefault(b"abc") is None
    assert t[b"abc"] is None

    # Test if setdefault correctly makes the trie own a reference to inserted
    # values. Without a proper reference the following may result in a memory
    # corruption error.
    v = t.setdefault(b"hello", [])
    v.append(5)
    del v # important, delete current reference.
    v = t[b"hello"]
    v.append(5)

def test_pop():
    t = Trie()
    with pytest.raises(KeyError):
        t.pop(b"a")
    assert t.pop(b"a", None) is None
    val = [1,2,3]
    assert t.pop(b"a", val) is val
    t[b"a"] = 5
    assert b"a" in t
    assert t.pop(b"a") == 5
    assert not b"a" in t

    # Test popping several keys
    n = 1000
    for i in xrange(n):
        t[b(str(i))] = i
    assert len(t) == n 
    numbers = [1, 808, 58, 256, 30, 905]
    for i in numbers:
        si = b(str(i))
        assert si in t
        assert t.pop(si, 123) == i
        assert not si in t
    assert len(t) == n - len(numbers)

def test_popitem():
    t = Trie()
    with pytest.raises(KeyError):
        t.popitem()
    t[b"hello"] = "world"
    assert len(t) == 1
    assert b"hello" in t
    assert t.popitem() == ("hello", "world")
    assert not b"hello" in t
    assert len(t) == 0

    n = 1000
    for i in xrange(n):
        t[b(str(i))] = i
    assert b"234" in t
    s = set()
    while t:
        s.add(t.popitem())
    assert s == set([(str(i), i) for i in xrange(n)])
    assert not b"234" in t

def test_iter():
    """Test iterating over a Trie"""
    t = Trie()
    words = [b"hello", b"foo", b"foobar", b"foozle"]
    for key in words:
        t[key] = 1
    assert set([b(key) for key in t]) == set(words)

    # Modify values in the trie using t's iterator
    for key in t:
        t[b(key)] = 2
    assert t.values() == [2]*len(words)

    # It should be an error to continue to iterate after adding and/or removing
    # nodes.
    i1 = iter(t)
    next(i1) # This should be fine
    t[b"new"] = 1
    with pytest.raises(RuntimeError):
        next(i1)
    # Alright, so addition was detected, now for deletion
    i2 = iter(t)
    del t[b"new"]
    with pytest.raises(RuntimeError):
        next(i2)
    # Detection should not be on number of nodes, so let's add and remove a
    # single node.
    i3 = iter(t)
    next(i3) # should be fine
    n = t.num_nodes()
    t[b"a"] = 1
    assert t.num_nodes() == n + 1
    del t[b"a"]
    assert t.num_nodes() == n
    with pytest.raises(RuntimeError):
        next(i3)

def test_keys_items_values():
    t = Trie()
    t[b"foo"] = 5
    assert t.keys() == ["foo"]
    assert t.values() == [5]
    assert t.items() == [("foo", 5)]
    t[b"foobar"] = 3
    t[b"hello"] = 7
    assert set(t.keys()) == set(["foo", "foobar", "hello"])
    assert set(t.values()) == set([5, 3, 7])
    assert set(t.items()) == set([("foo", 5), ("foobar", 3), ("hello", 7)])

    t = Trie()
    n = 1000
    for i in xrange(n):
        t[b(str(i))] = i
    assert len(t.keys()) == n
    values = xrange(n)
    keys = [str(x) for x in values]
    assert set(t.keys()) == set(keys)
    assert set(t.values()) == set(values)
    assert set(t.items()) == set(zip(keys, values))

def test_iterkeys_itervalues_iteritems():
    t = Trie()
    t[b"foo"] = 1 
    assert next(t.iterkeys()) == "foo"
    assert next(t.itervalues()) == 1
    assert next(t.iteritems()) == ("foo", 1)
    t[b"foobar"] = 999
    t[b"hello"] = 404
    assert set(t.iterkeys()) == set(["foo", "foobar", "hello"])
    assert set(t.itervalues()) == set([1, 999, 404])
    assert set(t.iteritems()) == set([("foo", 1), ("foobar", 999),
        ("hello", 404)])

    t = Trie()
    n = 1000
    for i in xrange(n):
        t[b(str(i))] = i
    assert len(list(t.iterkeys())) == n
    values = xrange(n)
    keys = [str(x) for x in values] 
    assert set(t.iterkeys()) == set(keys)
    assert set(t.itervalues()) == set(values)
    assert set(t.iteritems()) == set(zip(keys, values))

def test_num_nodes():
    t = Trie()
    assert t.num_nodes() == 0
    t[b"foo"] = 1
    assert t.num_nodes() == 3
    t[b"foobar"] = 1
    assert t.num_nodes() == 6
    t[b"foozle"] = 1
    assert t.num_nodes() == 9
    t[b"hello"] = 1
    assert t.num_nodes() == 14
    del t[b"foo"]
    assert not b"foo" in t
    assert t.num_nodes() == 14
    del t[b"foozle"]
    assert t.num_nodes() == 11
    del t[b"foobar"]
    assert t.num_nodes() == 5
    del t[b"hello"]
    assert t.num_nodes() == 0
    n = 100
    for i in xrange(n):
        t[b(str(i))] = i
    assert t.num_nodes() == n

def test_has_node():
    t = Trie()
    assert t.has_node(b"")
    assert not t.has_node(b"a")
    t[b"Hello"] = 0
    assert t.has_node(b"He")
    with pytest.raises(KeyError):
        t[b"He"]
    assert t.has_node(b"Hello")
    assert not t.has_node(b"Hello!")
    assert t[b"Hello"] == 0

def test_longest_prefix():
    t = Trie()
    assert t.longest_prefix(b"foobar") is None
    t[b"fo"] = 1
    t[b"foo"] = 2
    assert t.longest_prefix(b"foobar") == ("foo", 2)
    t[b"foobar"] = 3
    assert t.longest_prefix(b"foobar") == ("foobar", 3)
    assert t.longest_prefix(b"foozle") == ("foo", 2)
    del t[b"foo"]
    assert t.longest_prefix(b"foozle") == ("fo", 1)

def test_suffixes():
    t = Trie()
    t[b"production"] = 1
    t[b"productivity"] = 2
    t[b"process"] = 3
    t[b"prom"] = 4
    t[b"proper"] = 5
    t[b"promiss"] = 6
    t[b"prophet"] = 7
    t[b"professional"] = 8
    t[b"professor"] = 9
    assert set(t.suffixes(b"product")) == set([
        ("ion", 1),
        ("ivity", 2)])
    assert next(t.suffixes(b"proc")) == ("ess", 3)
    assert set(t.suffixes(b"prom")) == set([("", 4), ("iss", 6)])
    assert set(t.suffixes(b"prop")) == set([("het", 7), ("er", 5)])
    assert set(t.suffixes(b"pro")) == set([
        ("duction", 1),
        ("ductivity", 2),
        ("cess", 3),
        ("m", 4),
        ("per", 5),
        ("miss", 6),
        ("phet", 7),
        ("fessional", 8),
        ("fessor", 9)])

def test_neighbors():
    neighbors = lambda t,s,maxhd: set([
        tuple(x) for x in t.neighbors(s = s, maxhd = maxhd)])

    # Test that searching for a variant of a string does not return the string
    # itself.
    t = Trie()
    t[b"hello"] = (1,2,3)
    assert list(t.neighbors(b"hello", 5)) == []

    # Test searching for variants of non-existent string raises an exception.
    with pytest.raises(Exception):
        list(t.neighbors(b"he", 1))
    with pytest.raises(Exception):
        list(t.neighbors(b"h3llo", 5))

    t[b"h3llo"] = None
    assert list(t.neighbors(b"hello", 1)) == [(1, "h3llo", None)]

    # Test that giving a bad maxhd value raises an ValueError
    with pytest.raises(ValueError):
        list(t.neighbors(b"hello", -1))
    with pytest.raises(ValueError):
        list(t.neighbors(b"hello", 0))

    # Testing getting variant strings
    t = Trie()
    t[b"hello world"] = 0
    t[b"*ello world"] = 1
    t[b"*ell* world"] = 2
    t[b"*ell* w*rld"] = 3
    t[b"hell* w*rl*"] = 3

    correct_neighbors = [
            (1, "*ello world", 1),
            (2, "*ell* world", 2),
            (3, "*ell* w*rld", 3),
            (3, "hell* w*rl*", 3)]
    assert neighbors(t, b"hello world", 3) == set(correct_neighbors)
    assert neighbors(t, b"hello world", 1) == set(correct_neighbors[:1])

    correct_neighbors = [\
            (1, "*ello world", 1),
            (1, "*ell* w*rld", 3)]
    assert neighbors(t, b"*ell* world", 1) == set(correct_neighbors)

    # This test revealed a mistake in an older trie implementation, where the
    # current hamming distance got overwritten when child nodes were pushed
    # onto a stack.
    strings = [b("".join(p)) for p in product("ABC", "ABC", "ABC")]
    for s in strings:
        t[s] = 0
    assert len(neighbors(t, b"AAA", 1)) == 6

def eqp(pairs, x):
    """test if pairs and x are equal."""
    if len(pairs) != len(x):
        return False
    for pair in pairs:
        if not (pair in x or pair[::-1] in x):
            return False
    return True

def test_pairs():
    get_pairs = lambda t, keylen, maxhd:[(s1, s2) \
            for hd, s1, value1, s2, value2 in t.pairs(keylen, maxhd)]

    # Test that giving a bad maxhd or keylen value raises an ValueError
    t = Trie()
    t[b"hello"] = 0
    assert list(t.pairs(keylen = 5, maxhd = 1)) == []
    with pytest.raises(ValueError):
        list(t.pairs(5, -1))
    with pytest.raises(ValueError):
        list(t.pairs(5, 0))
    with pytest.raises(ValueError):
        list(t.pairs(-1, 1))

    # The pairs iterator modifies nodes in the trie to keep track of its
    # progress. Running multiple such iterators would cause inconsistent
    # results, hence the iterator should throw an exception if another is
    # active.
    t = Trie()
    t[b"hello"] = 0
    t[b"h3llo"] = 1
    assert eqp([("hello", "h3llo")], get_pairs(t, 5, 1))
    i1 = t.pairs(5, 1)
    i2 = t.pairs(5, 1)
    with pytest.raises(RuntimeError):
        next(i1)
    assert next(i2) == (1, "hello", 0, "h3llo", 1)

    # One should be able to modify the trie if a dirty iterator is active.
    t = Trie()
    it = t.pairs(1,1)
    t[b"abc"] = 1

    t = Trie()
    assert list(t.pairs(keylen = 4, maxhd = 5)) == []
    t[b"AAAA"] = 0
    assert list(t.pairs(4, 5)) == []
    t[b"AAAT"] = 0
    assert eqp([("AAAA", "AAAT")], get_pairs(t, 4, 1))
    t[b"ATAT"] = 0
    assert eqp([("AAAA", "AAAT"),
            ("ATAT", "AAAT")], get_pairs(t, 4, 1))
    assert eqp([("AAAA", "AAAT"),
            ("ATAT", "AAAT"),
            ("ATAT", "AAAA")], get_pairs(t, 4, 2))
    # Test inserting single different length prefix does not affect result.
    t[b"AA"] = 0
    assert eqp([("AAAA", "AAAT"),
            ("ATAT", "AAAT"),
            ("ATAT", "AAAA")], get_pairs(t, 4, 2))
    t[b"AT"] = 0
    assert eqp([("AAAA", "AAAT"),
            ("ATAT", "AAAT"),
            ("ATAT", "AAAA")], get_pairs(t, keylen = 4, maxhd = 2))
    assert eqp([("AA", "AT")], get_pairs(t, keylen = 2, maxhd = 2))
    # Double check that eqp works:
    assert not eqp([("AAAA", "AAAT"),
            ("ATAT", "AAAT"),
            ("ATAT", "AAAA"),
            ("AA", "AC")], get_pairs(t, 4, 2))

    # Test where only 1 string should not be in any of the pairs because of
    # maximum hamming distance limitation.
    t = Trie()
    t[b"AAAA"] = 1
    t[b"AAAT"] = 2
    t[b"TAAT"] = 3
    t[b"TATA"] = 4
    assert eqp([("AAAA", "AAAT"), ("TAAT", "AAAT")],
            get_pairs(t, 4, 1));

    # Test larger set of pairs
    t2 = Trie()
    for s in [b("{:08b}".format(i)) for i in range(256)]:
        t2[s] = 1 # "00000000" to "11111111"
    # Each string has 8 neighbors at HD 1. So the number of pairs, ignoring
    # order, should be ( 256 * 8 ) / 2
    assert len(list(t2.pairs(keylen = 8, maxhd = 1))) == (256*8)/2
    # Test hd = 2:
    # There are (8 choose 2) + (8 choose 1) = 36 neighbors for every number.
    assert len(list(t2.pairs(keylen = 8, maxhd = 2))) == (256*36)/2
    # There are (8 choose 3) + (8 choose 2) + (8 choose 1) = 92 neighbors for
    # every number.
    assert len(list(t2.pairs(keylen = 8, maxhd = 3))) == (256*92)/2

    # Test that hamming distance and nodes are correct
    for hd, s1, value1, s2, value2 in t2.pairs(keylen = 8, maxhd = 3):
        assert hd == sum([ch1 != ch2 for ch1, ch2 in zip(s1, s2)])
        assert value1 is t2[b(s1)]
        assert value2 is t2[b(s2)]

    for s in [b("{:04b}".format(i)) for i in range(16)]:
        t2[s] = 1 # "0000" to "1111"
    assert len(list(t2.pairs(keylen = 4, maxhd = 1))) == (16*4)/2

    # Test pairs where nodes can have 3 children
    # NOTE: this test is important! It revealed a mistake in an older trie
    # implementation. The mistake was that it used a field (the hamming
    # distance field) that was being overwritten when children of a node were
    # being pushed onto a stack. (solution was saving the hd field before
    # pushing children).
    t = Trie()
    strings = [b("".join(p)) for p in product("ABC", "ABC", "ABC")]
    for s in strings: 
        t[s] = 0
    assert len(list(t.pairs(3, 1))) == 27 * 6 / 2
    # There are 3 ways to select two mutated positions, for each mutated
    # position there are 2 variants, so there are 27 strings with each 12
    # possible neighbors, divide by two to prevent overcounting.
    assert len(list(t.pairs(3, 2))) == (27 * 3 * 4 / 2) + (27 * 6 / 2)
    assert len(list(t.pairs(3, 3))) == (27 * 26) / 2

    # Test garbage collection of pairs iterator. This test can reveal a mistake
    # in properly resetting the trie when the iterator is garbage collected.
    t = Trie()
    strings = [b("".join(p)) for p in product("ABC", "ABC", "ABC")]
    for s in strings: 
        t[s] = 0
    # Only go through the pairs partially
    for i, pair in enumerate(t.pairs(3,3)):
        if i > 100:
            break
    assert len(list(t.pairs(3,3))) == (27 * 26) / 2
