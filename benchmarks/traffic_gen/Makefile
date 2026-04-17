CC = gcc
#CC = icc
MPICC = mpicc
CFLAGS = -Wall -march=x86-64 -mtune=generic
CFLAGS += -fopenmp
LDFLAGS = -pthread -lrt
# CFLAGS += -no-multibyte-chars #added to work around the error Catastrophic error: could not set locale "" to allow processing of multibyte characters

SRCDIR=src
BUILDDIR=build



PRG_SUFFIX=.x
TARGET=traffic_gen$(PRG_SUFFIX)

all: init $(TARGET)

init:
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/nop.o: $(SRCDIR)/nop.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/utils.o: $(SRCDIR)/utils.c
	$(CC) $(CFLAGS) -Wno-unused-but-set-variable -O0 -c $< -o $@

$(BUILDDIR)/traffic_gen_c.o: $(SRCDIR)/traffic_gen.c
	$(MPICC) $(CFLAGS)  -O0 -c $< -o $@

$(TARGET): $(BUILDDIR)/nop.o $(BUILDDIR)/utils.o $(BUILDDIR)/traffic_gen_c.o
	$(MPICC) $(CFLAGS)  -O0 $(LDFLAGS) $^ -o $@

clean:
	rm -f $(BUILDDIR)/*.o *.x
