include Makefile.local

ifndef BINDIR
$(error Fatal: Makefile.local must define BINDIR variable)
endif

ifndef CPP
$(error Fatal: Makefile.local must define CPP variable)
endif

#
# About the RPC: this experiment uses ZeroMQ for the messaging (sockets)
# and msgpack for the message encoding (wire format).  This will allow clients
# to be written in Python and other languages as well as C++.
#
# ZeroMQ is available in homebrew;
#
#    brew install zeromq
#
# We use C++ bindings for zeromq, available in the separate project cppzqm:
#
#    git clone https://github.com/zeromq/cppzmq.git
#
# (it is a header-only library)
#
# For msgpack,
#
#    brew install msgpack
#
#
#######
#
# Testing google protocol buffers (protobuf) v3 for the message encoding.
#
#     brew install protobuf
#


BINARIES=ch-frb-l1 ch-frb-simulate-l0 rpc-client

all: $(BINARIES)

INCFILES = ch_frb_rpc.hpp rpc.pb.h

L1_OBJS = ch-frb-l1.o ch_frb_rpc.o rpc.pb.cc

rpc.pb.cc rpc.pb.h: rpc.proto
	protoc --cpp_out . $^

# Compile flags
CPP_CFLAGS = -I$(CPPZMQ_INC_DIR) -I$(MSGPACK_INC_DIR) $(PROTOBUF_INC)

%.o: %.cpp $(INCFILES)
	$(CPP) -c -o $@ $< $(CPP_CFLAGS)

# Duplicated for auto-generated protobuf file rpc.pb.cc
%.o: %.cc $(INCFILES)
	$(CPP) -c -o $@ $< $(CPP_CFLAGS)

rpc-client: rpc_client.o rpc.pb.o
	$(CPP) -o $@ $^ $(CPP_LFLAGS) -lch_frb_io -lzmq $(PROTOBUF_LIB)

ch-frb-l1: $(L1_OBJS)
	$(CPP) -o $@ $^ $(CPP_LFLAGS) -lch_frb_io -lzmq $(PROTOBUF_LIB)

ch-frb-simulate-l0: ch-frb-simulate-l0.cpp
	$(CPP) -o $@ $< $(CPP_LFLAGS) -lch_frb_io

clean:
	rm -f *.o *~ $(BINARIES)

install:
	echo 'Nothing to install here!'
