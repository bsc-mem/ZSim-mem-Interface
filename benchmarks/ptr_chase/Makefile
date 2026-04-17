CC=g++
TARGET=ptr_chase
TARGET_ARRAYGEN=arraygen

# Architectural details
cache_line_size = 64

# Benchmark configuration
array_size = 1073741824 # using calculator to improve number readability
instructions = 1000000000
iterations = 5000

# Compute derived parameters
array_elems := $(shell echo $$(($(array_size) / $(cache_line_size))))
loop_instr  := $(shell echo $$(($(instructions) / $(iterations))))

all: ptr_chase array

ptr_chase: ptr_chase.c loop.h
	@echo "Compiling ptr_chase with:"
	@echo "  ARRAY_ELEMS = $(array_elems)"
	@echo "  ITERS       = $(iterations)"
	$(CC) -O0 -DARRAY_ELEMS=$(array_elems) -DITERS=$(iterations) -o $(TARGET) ptr_chase.c

loop.h: loop_template.h
	@echo "Generating loop.h ..."
	@echo "  loop_instr = $(loop_instr)"
	awk -v n=$(loop_instr) '{for(i=0; i<n; i++) print $$0}' loop_template.h > loop.h
	@echo "Generated loop.h with $$(wc -l < loop.h) lines"

arraygen: arraygen.c
	@echo "Compiling arraygen with:"
	@echo "  ARRAY_ELEMS = $(array_elems)"
	$(CC) -O3 -DARRAY_ELEMS=$(array_elems) -o $(TARGET_ARRAYGEN) arraygen.c

array: arraygen
	@echo "Generating array.dat ..."
	./$(TARGET_ARRAYGEN)
	@echo "array.dat generated"

clean:
	rm -f $(TARGET) $(TARGET_ARRAYGEN)
	rm -f loop.h	

fullclean: clean
	rm -f array.dat

.PHONY: all array clean fullclean
