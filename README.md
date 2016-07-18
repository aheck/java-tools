# java-tools #

Some __experimental__ command-line tools for Java

## Dependencies ##

These tools are written in C and depend on GLib2, libzip, sqlite3 and
libclassreader. To build them you need cmake 3.0 or newer.

## Tools ##

- __java-classinfo__: Dump information about a .class file
- __java-findjar__: Find a JAR file that contains a given Java class
- __java-indexproject__: Create or update an index of compiled Java classes
    and their methods in an SQLite 3 database (.class files can be in
    directories or JAR archives)

## Build It ##

First you need to install
[libclassreader](https://github.com/aheck/libclassreader). Then you can build
and install it with:

```bash
$ cmake .
$ make
$ make install
```

## License ##

java-tools are licensed under the MIT license

## Contact ##

Andreas Heck <<aheck@gmx.de>>
