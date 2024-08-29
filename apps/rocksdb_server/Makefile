SKYLOFT_DIR ?=
ROCKSDB_SRC ?= 

CFLAGS = -I$(SKYLOFT_DIR)/include -DSKYLOFT
SKYLOFT_LIBS = $(SKYLOFT_DIR)/lib/libskyloft.a $(SKYLOFT_DIR)/lib/libutils.a
SKYLOFT_LDFLAGS = -T $(SKYLOFT_DIR)/lib/libos.ld -lnuma
SKYLOFT_LDFLAGS += $(shell pkg-config --libs libdpdk)

CFLAGS += -I$(ROCKSDB_SRC)/include/rocksdb/
ROCKSDB_LIBS = $(ROCKSDB_SRC)/librocksdb.a
ROCKSDB_LDFLAGS = -lpthread -ldl -lz -lbz2 -lsnappy -lzstd -lstdc++ -lm

ifneq ($(DEBUG),)
CFLAGS += -DDEBUG -rdynamic -O0 -ggdb
LDFLAGS += -rdynamic
else
CFLAGS += -DNDEBUG -O3
endif

CC = gcc
CXX = g++
LD = gcc

rocksdb_server_src = rocksdb_server.cc
rocksdb_server_obj = $(rocksdb_server_src:.cc=.o)

create_db_src = create_db.cc
create_db_obj = $(create_db_src:.cc=.o)

src = $(rocksdb_server_src) $(create_db_src)
obj = $(rocksdb_server_obj) $(create_db_obj)

# must be first
all: rocksdb_server create_db

$(ROCKSDB_LIBS):
	make -j -C rocksdb static_lib

rocksdb_server: $(rocksdb_server_obj) $(SKYLOFT_LIBS) $(ROCKSDB_LIBS)
	$(LD) -o $@ $^ $(SKYLOFT_LDFLAGS) $(ROCKSDB_LDFLAGS)

create_db: $(create_db_obj) $(ROCKSDB_LIBS)
	$(LD) -o $@ $^ $(ROCKSDB_LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cc
	$(CXX) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(obj) rocksdb_server