# pg_consul/playground/GNUmakefile

CONTRIB_OBJS	= $(patsubst %.cpp,%.o,$(wildcard *--*.cpp))

OBJS		= $(patsubst %.cpp,%.o,$(wildcard consul_*.cpp))
BINS		= consul_kv consul_leader consul_peers

CPPFLAGS+=-pedantic -Wall
# CPPFLAGS+=-Wno-deprecated-register -Wno-unused-local-typedef
CPPFLAGS+=-I../include
CPPFLAGS+=-std=c++14 -stdlib=libc++
LDFLAGS=-lcurl -std=c++14 -stdlib=libc++

all: ${BINS}

%.o: %.cpp
	${CXX} -c ${CPPFLAGS} -o $@ $<

consul_kv: consul_kv.o ${CONTRIB_OBJS}
	${CXX} ${LDFLAGS} -o $@ $^

consul_leader: consul_leader.o ${CONTRIB_OBJS}
	${CXX} ${LDFLAGS} -o $@ $^

consul_peers: consul_peers.o ${CONTRIB_OBJS}
	${CXX} ${LDFLAGS} -o $@ $^

loast-test-keys::
	curl -X PUT -d 'test1-value' http://127.0.0.1:8500/v1/kv/test/key1; echo
	curl -X PUT -d 'test2-value' http://127.0.0.1:8500/v1/kv/test/key2; echo

test:: ${BINS}
	./consul_leader
	./consul_peers
	./consul_kv -k=test/key1
	./consul_kv -k=test -r

clean::
	rm -f ${BINS} *.o
