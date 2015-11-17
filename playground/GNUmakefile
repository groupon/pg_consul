# pg_consul/playground/GNUmakefile

CONTRIB_OBJS	= $(patsubst %.cpp,%.o,$(wildcard *--*.cpp))

OBJS		= $(patsubst %.cpp,%.o,$(wildcard consul_*.cpp))
BINS		= consul-kv consul-status

CPPFLAGS+=-pedantic -Wall
# CPPFLAGS+=-Wno-deprecated-register -Wno-unused-local-typedef
CPPFLAGS+=-I../include
CPPFLAGS+=-std=c++14 -stdlib=libc++
LDFLAGS=-lcurl -std=c++14 -stdlib=libc++

all: ${BINS}

%.o: %.cpp
	${CXX} -c ${CPPFLAGS} -o $@ $<

consul-kv: consul_kv.o ${CONTRIB_OBJS}
	${CXX} ${LDFLAGS} -o $@ $^

consul-status: consul_status.o ${CONTRIB_OBJS}
	${CXX} ${LDFLAGS} -o $@ $^

load-test-keys::
	curl -X PUT -d 'test-value' http://127.0.0.1:8500/v1/kv/test; echo
	curl -X PUT -d 'test1-value' http://127.0.0.1:8500/v1/kv/test/key1; echo
	curl -X PUT -d 'test2-value' http://127.0.0.1:8500/v1/kv/test/key2; echo

test:: ${BINS}
	./consul-status -m=leader
	./consul-status -m=peers
	./consul-status -m=all
	./consul-kv -k=test/key1
	./consul-kv -k=test -r

clean::
	rm -f ${BINS} *.o
