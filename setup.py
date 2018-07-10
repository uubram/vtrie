from setuptools import setup
from distutils.extension import Extension
from os import path
from glob import glob

here = path.abspath(path.dirname(__file__))

with open(path.join(here, "README.rst")) as f:
    long_description = f.read()

setup(
        name = "vtrie",
        version = "0.0.3",
        description = "A trie structure supporting fuzzy string searches.",
        long_description = long_description,
        author = "Bram Gerritsen",
        author_email = "bramgerr1@gmail.com",
        license = "GPLv3",
        classifiers = [
            "Development Status :: 4 - Beta",
            "Intended Audience :: Developers",
            "Intended Audience :: Science/Research",
            "License :: OSI Approved :: GNU General Public License v3 (GPLv3)",
            "Programming Language :: Python :: 2",
            "Programming Language :: Python :: 2.6",
            "Programming Language :: Python :: 2.7",
            "Programming Language :: Python :: 3",
            "Programming Language :: Python :: 3.2",
            "Programming Language :: Python :: 3.3",
            "Programming Language :: Python :: 3.4",
            "Programming Language :: Python :: 3.5",
            "Programming Language :: Python :: Implementation :: CPython",
            "Topic :: Scientific/Engineering :: Information Analysis",
            "Topic :: Software Development :: Libraries :: Python Modules",
            "Topic :: Text Processing :: Linguistic",
            "Topic :: Text Processing :: Indexing",
            ],
        keywords = "trie tree datastructure dictionary Hamming distance",
        ext_modules = [
            Extension("vtrie",
                sources = glob("src/*.c"),
                include_dirs = ["include"],
                language = "c",
                extra_compile_args = ["-std=c99", "-pedantic", "-Wall", 
                    "-Wextra", "-O3"],
                )])
