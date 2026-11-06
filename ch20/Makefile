CC = cc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -O2
SRC = chunk.c compiler.c debug.c memory.c object.c scanner.c table.c value.c vm.c

clox: main.c $(SRC)
	$(CC) $(CFLAGS) main.c $(SRC) -o clox -lm

test: test_table.c $(SRC)
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined test_table.c $(SRC) -o test_table -lm
	./test_table

# Runs the whole suite, then repeats the parts that a setting should change.
bench: bench_table.c $(SRC)
	$(CC) $(CFLAGS) bench_table.c $(SRC) -o bench_table -lm
	$(CC) $(CFLAGS) -DNAIVE_NUMBER_HASH bench_table.c $(SRC) -o bench_naive -lm
	$(CC) $(CFLAGS) -DTABLE_MAX_LOAD=0.5 bench_table.c $(SRC) -o bench_load50 -lm
	$(CC) $(CFLAGS) -DTABLE_MAX_LOAD=0.9 bench_table.c $(SRC) -o bench_load90 -lm
	./bench_table
	@echo "--- load factor 0.5"
	./bench_load50
	@echo "--- load factor 0.9"
	./bench_load90
	@echo "--- naive number hash"
	./bench_naive --numbers

clean:
	rm -f clox test_table bench_table bench_naive bench_load50 bench_load90
