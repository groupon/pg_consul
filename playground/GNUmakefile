# pg_consul/playground/GNUmakefile

CPR_OBJS	= $(patsubst %.cpp,%.o,$(wildcard *--*.cpp))

OBJS		= $(patsubst %.cpp,%.o,$(wildcard consul_*.cpp))
BINS		= consul_leader consul_peers

CPPFLAGS+=-pedantic -Wall
# CPPFLAGS+=-Wno-deprecated-register -Wno-unused-local-typedef
CPPFLAGS+=-I../include
CPPFLAGS+=-std=c++14 -stdlib=libc++
LDFLAGS=-lcurl -std=c++14 -stdlib=libc++

all: ${BINS}

%.o: %.cpp
	${CXX} -c ${CPPFLAGS} -o $@ $<

consul_leader: consul_leader.o ${CPR_OBJS}
	${CXX} ${LDFLAGS} -o $@ $^

consul_peers: consul_peers.o ${CPR_OBJS}
	${CXX} ${LDFLAGS} -o $@ $^

test:: ${BINS}
	./consul_leader
	./consul_peers

clean::
	rm -f ${BINS} *.o
