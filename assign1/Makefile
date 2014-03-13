<<<<<<< HEAD


test_assign1: test_assign1_1.c storage_mgr.c storage_mgr.h
	gcc -I. -g -c -o storage_mgr.o storage_mgr.c
	gcc -I. -g -c -o test_assign1_1.o test_assign1_1.c
	gcc -I. -o test_assign1 test_assign1_1.o storage_mgr.o

clean:
	rm -rf *.o test_assign1
=======
OBJ=storage_mgr.o dberror.o
TEST_OBJ=test_assign1_1.o
TEST_BIN=test_assign1
CFLAGS:=$(CFLAGS) -I. -g

all: $(TEST_BIN) 

$(TEST_BIN): $(OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(TEST_OBJ)

clean:
	rm -f $(TEST_BIN) $(OBJ) $(TEST_OBJ)
>>>>>>> instructor
