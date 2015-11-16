pg_consul 0.10.0
=============

[![PGXN version](https://github.com/groupon/pg_consul)](https://github.com/groupon/pg_consul)

This library contains a single PostgreSQL extension, "pg_consul".  It is a
set of functions that are used to query and interact with a
[consul](https://www.consul.io/) cluster.

Documentation
------------

See [docs](https://github.com/groupon/pg_consul/blob/master/doc/pg_consul.md)
for details on how to use consul.

```sql
# SELECT consul_status_leader();
  consul_status_leader
-----------------------
 127.0.0.1:8300
(1 row)

Time: 4.024 ms
# SELECT * FROM consul_status_peers();
  hostname   | port | leader
-------------+------+--------
 10.23.9.162 | 8300 | f
 127.0.0.1   | 8300 | t
(2 rows)

Time: 5.672 ms
```


Installation
------------

To build pg_consul:

    make
    make install
    make installcheck

If you encounter an error such as:

    "Makefile", line 8: Need an operator

You need to use GNU make, which may well be installed on your system as
`gmake`:

    gmake
    gmake install
    gmake installcheck

If you encounter an error such as:

    make: pg_config: Command not found

Be sure that you have `pg_config` installed and in your path. If necessary
tell the build process where to find it:

    env PG_CONFIG=/path/to/pg_config make && make installcheck && make install

If you encounter an error such as:

    ERROR:  must be owner of database regression

You need to run the test suite using a super user, such as the default
"postgres" super user:

    make installcheck PGUSER=postgres

A modern C++ compiler that supports C++14 is required.

Once pg_consul is installed, you can add it to a database by running:

    CREATE EXTENSION pg_consul;


Dependencies
------------

Other than PostgreSQL, `pg_consul` has a runtime dependency on
[cURL](http://curl.haxx.se/).

`pg_consul` has buildtime dependencies on:

- GNU make
- C++14 standards compliant compiler
- cURL header files

All other buildtime requirements are included.

Copyright and License
---------------------

Copyright (c) 2015, Groupon, Inc.

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose, without fee, and without a written agreement
is hereby granted, provided that the above copyright notice and this
paragraph and the following two paragraphs appear in all copies.

IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE TO ANY PARTY FOR
DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
EVEN IF THE AUTHORS OR COPYRIGHT HOLDERS HAVE BEEN ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.

THE AUTHORS AND COPYRIGHT HOLDERS SPECIFICALLY DISCLAIM ANY WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN
"AS IS" BASIS, AND THE AUTHORS AND COPYRIGHT HOLDERS HAVE NO OBLIGATION TO
PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
