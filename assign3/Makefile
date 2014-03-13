SOURCES= \
lru_linked_list.c\
buffer_mgr.c \
buffer_mgr_stat.c \
dberror.c \
page_table.c \
storage_mgr.c \
record_mgr.c \
rm_serializer.c \
expr.c

HEADERS = storage_mgr.h \
dberror.h \
page_table.h \
record_mgr.h \
expr.h \
lru_linked_list.h\
buffer_mgr.h \
buffer_mgr_stat.h 

EXESRC1=test_assign3_1.c
EXESRC2=test_expr.c

EXECUTABLE1=test_assign3
EXECUTABLE2=test_expr

CC=cc
CFLAGS=-c -Wall -g -I.
LDFLAGS=-pthread
OBJECTS=$(SOURCES:.c=.o)
EXEOBJ1=$(EXESRC1:.c=.o)
EXEOBJ2=$(EXESRC2:.c=.o)

all: $(HEADERS) $(SOURCES) $(EXECUTABLE1) $(EXECUTABLE2)

$(EXECUTABLE1): $(OBJECTS) $(EXEOBJ1)
	$(CC) $(OBJECTS) $(EXEOBJ1) -o $@ $(LDFLAGS) 

$(EXECUTABLE2): $(OBJECTS) $(EXEOBJ2)
	$(CC) $(OBJECTS) $(EXEOBJ2) -o $@ $(LDFLAGS) 

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf *.o test_assign3 test_expr test_table_r

test: $(EXECUTABLE1) $(EXECUTABLE2)
	rm -rf test_table_r
	./test_assign3
	./test_expr

valgrindtest: $(EXECUTABLE1) $(EXECUTABLE2)
	rm -rf testbuffer.bin
	echo Valgrind Output For $(EXECUTABLE1):-
	valgrind --log-file=valgrind1.out $(EXECUTABLE1)
	cat valgrind1.out
	echo Valgrind Output For $(EXECUTABLE1):-
	valgrind --log-file=valgrind2.out $(EXECUTABLE2)
	cat valgrind2.out
