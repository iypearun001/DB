<<<<<<< HEAD

SOURCES= \
lru_linked_list.c\
lru_linked_list.h\
buffer_mgr.c \
buffer_mgr.h \
buffer_mgr_stat.c \
buffer_mgr_stat.h \
dberror.c \
dberror.h \
page_table.c \
page_table.h \
storage_mgr.c \
storage_mgr.h \
record_mgr.c \
record_mgr.h \
rm_serializer.c \
expr.c \
btree_mgr.c \
btree_mgr.h \
expr.h

EXESRC1=test_assign4_1.c

EXECUTABLE1=test_assign4

CC=cc
CFLAGS=-c -Wall -g -I.
LDFLAGS=-pthread
OBJECTS=$(SOURCES:.c=.o)
EXEOBJ1=$(EXESRC1:.c=.o)

all: $(SOURCES) $(EXECUTABLE1)
	
$(EXECUTABLE1): $(OBJECTS) $(EXEOBJ1)
	$(CC) $(OBJECTS) $(EXEOBJ1) -o $@ $(LDFLAGS) 

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf *.o test_assign4 testidx

test: $(EXECUTABLE1)
	rm -rf testidx
	./$(EXECUTABLE1)

valgrindtest: $(EXECUTABLE1)
	rm -rf testidx
	echo Valgrind Output For $(EXECUTABLE1):-
	valgrind --log-file=valgrind1.out $(EXECUTABLE1)
	cat valgrind1.out
=======
OBJ=expr.o dberror.o rm_serializer.o record_mgr.o btree_mgr.o
HEADERS=buffer_mgr.h dberror.h expr.h record_mgr.h storage_mgr.h tables.h test_helper.h btree_mgr.h
TEST_BIN=test_expr.bin test_assign4_1.bin
TEST_OBJ=$(TEST_BIN:.bin=.o)
CFLAGS:=$(CFLAGS) -I. -g -Wall -Werror

all: $(TEST_BIN) $(OBJ) $(TEST_OBJ) $(HEADERS)

$(OBJ): $(HEADERS)

%.bin: %.o $(OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(@:.bin=.o)

clean:
	rm -f $(TEST_BIN) $(OBJ) $(TEST_OBJ)

.PHONY: clean
>>>>>>> instructor
