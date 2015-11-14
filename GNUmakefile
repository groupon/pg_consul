# pg_consul/GNUmakefile

# Maintainer vars
CONTRIB_DIR=contrib
CPR_DIR=${CONTRIB_DIR}/cpr

# BEGIN: pgxs vars
MODULE_big	= pg_consul
OBJS		= $(patsubst %.cpp,%.o,$(wildcard src/*.cpp))

EXTENSION	= pg_consul
EXTVERSION	= $(shell grep -m 1 '[[:space:]]\{8\}"version":' META.json | \
		  sed -e 's/[[:space:]]*"version":[[:space:]]*"\([^"]*\)",\{0,1\}/\1/')
DATA		= $(wildcard sql/*--*.sql)
DOCS		= $(wildcard doc/*.md)
PGFILEDESC	= "pg_consul - SQL API to consul"

TESTS		= $(wildcard test/sql/*.sql)
REGRESS		= $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS	= --inputdir=test

PG_CPPFLAGS+=-pedantic -Wall
PG_CPPFLAGS+=-Wno-deprecated-register -Wno-unused-local-typedef
PG_CPPFLAGS+=-I./include
PG_CPPFLAGS+=-std=c++14
#PG_CPPFLAGS+=-fno-exceptions
SHLIB_LINK=-std=c++14 -stdlib=libc++ -lcurl
EXTRA_CLEAN	= sql/$(EXTENSION)--$(EXTVERSION).sql

PG_CONFIG	?= pg_config
PGXS		:= $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# FIXME(seanc@): This is a terrible pgxs hack.  In order to link a PostgreSQL
# shared object, use clang++ (or what is hopefully clang++ and not g++) as
# the linker.  Ideally it would be possible to specify this as a paramter to
# pgxs, something like PGXS_LD.  This variable needs to be overwritten after
# PGXS is included.
CXX=clang++
CC=${CXX}

all: sql/$(EXTENSION)--$(EXTVERSION).sql

consul consul1::
	mkdir -p $(shell pwd)/.consul1-data
	consul agent -server -bootstrap-expect=1 -data-dir=$(shell pwd)/.consul1-data -dc=pgc1 -node=server1 -bind=127.0.0.1

CONSUL2_IP=$(shell ifconfig en0 | grep inet | awk '{print $$2}')
consul2::
	mkdir -p $(shell pwd)/.consul2-data
	consul agent -server -data-dir=$(shell pwd)/.consul2-data -dc=pgc1 -node=server2 -bind=$(CONSUL2_IP) -client=$(CONSUL2_IP) -retry-join=127.0.0.1

consul3::
	mkdir -p $(shell pwd)/.consul3-data
	consul agent -server -bootstrap -data-dir=$(shell pwd)/.consul3-data -dc=pgc1 -node=server3 -bind=127.0.0.1

sql/$(EXTENSION)--$(EXTVERSION).sql: sql/$(EXTENSION).sql
	cp $< $@

.PHONY: results
results:
	rsync -avP --delete results/ expected/

dist:
	git archive --format zip --prefix=$(EXTENSION)-$(EXTVERSION)/ -o $(EXTENSION)-$(EXTVERSION).zip HEAD

## BEGIN: Maintainer targets
output:
	printf "DATA: %s\n" "${DATA}"
	printf "MODULES: %s\n" "${MODULES}"
	printf "MODULES_big: %s\n" "$(MODULE_big)"
	printf "REGRESS: %s\n" "$(REGRESS)"
	printf "docdir: %s\n" "$(docdir)"
	printf "srcdir: %s\n" "$(srcdir)"

psql::
	psql -U ${USER} -d contrib_regression

# import-cpr.sh is used to copy in curl sources into src/.  Noone should need
# to call this other than the maintainer.
import-cpr.sh::
	echo mkdir -p include/cpr > $@
	find ${CPR_DIR}/include -type f | awk '{print "cp " $$1 " include/cpr/`basename " $$1 "`"}' >> $@
	find ${CPR_DIR}/cpr -type f -name '*.cpp' | awk '{print "cp " $$1 " src/`dirname " $$1 " | perl -p -e \"s#/#\\n#g\" | tail -1 | perl -p -e \"s#\\n#-#g\"`-`basename " $$1 "`"}' >> $@
