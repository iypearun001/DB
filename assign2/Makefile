CFILES:= \
lru_linked_list.c\
buffer_mgr.c \
buffer_mgr_stat.c \
dberror.c \
page_table.c \
storage_mgr.c \

HEADERS:= \
dberror.h \
buffer_mgr_stat.h \
buffer_mgr.h \
lru_linked_list.h\
page_table.h \
storage_mgr.h \

SOURCES:=$(CFILES) $(HEADERS)

EXECUTABLE=test_assign2
E2=test_assign2_2

CC=cc
CFLAGS=-c -Wall -g -I.
LDFLAGS=-pthread
OBJECTS:=$(CFILES:.c=.o)

all: $(SOURCES) $(EXECUTABLE) $(E2)

$(EXECUTABLE): $(OBJECTS) test_assign2_1.o
	$(CC) $(LDFLAGS) $(OBJECTS) test_assign2_1.o -o $@  

$(E2): $(OBJECTS) test_assign2_2.o
	$(CC) $(LDFLAGS) $(OBJECTS) test_assign2_2.o -o $@

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf *.o test_assign2 test_assign2_2 testbuffer.bin

test: $(EXECUTABLE)
	rm -rf testbuffer.bin
	./test_assign2

valgrindtest: $(EXECUTABLE)
	rm -rf testbuffer.bin
	valgrind --log-file=valgrind.out ./test_assign2 
	echo Valgrind Output:-
	cat valgrind.out

.PHONY: clean
