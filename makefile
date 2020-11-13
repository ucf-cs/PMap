
BUILDTYPE ?= release

ifeq ($(BUILDTYPE),release)
  OPTFLAG ?= -O3 -march=native -flto
  DBGFLAG ?= -DNDEBUG=1
endif

ifeq ($(BUILDTYPE),debug)
  OPTFLAG ?= -O0 -g2 -march=native
  DBGFLAG ?= -ggdb
endif

WARNFLAG ?= #-Wall -Wextra -pedantic -Wl,--verbose
ARCHFLAG ?= -DDEFAULT_CACHELINE_SIZE=64 # should not be needed for a c++17 compliant compiler

DATAFILE ?= ./chashmap.dat #/mnt/pmem/pm1/hashtest.dat
VALGRIND ?= /home/marioman/local/bin/valgrind --tool=pmemcheck
VGFLAGS  ?= --flush-check=yes --flush-align=yes --mult-stores=yes
CHKARGS  ?= -n 1000000 -c 20 -f $(DATAFILE) -t 48 

INCLUDES := -I. -Iinclude -Icontainers -Itests -Ihashing -I/home/marioman/onefile -I/home/marioman/usr/include
LIBS     := -L/home/marioman/local/lib -L/home/marioman/usr/lib -lpmemobj #-L/usr/lib/x86_64-linux-gnu
TARGET   := ./bin/test.out
#HEADERS  := container-onefileMap.hpp container-pmemMap.hpp container-ucfMap.hpp container-stlMap.hpp

.PHONY: default
default: $(TARGET)

$(TARGET): runTest.cpp #$(HEADERS)
	mkdir -p ./bin
	$(CXX) -std=c++2a $(WARNFLAG) -pthread $(OPTFLAG) $(DBGFLAG) $(ARCHFLAG) $(INCLUDES) $(LIBS) -fuse-ld=gold $< -o $@

.PHONY: valcheck
valcheck: $(TARGET)
	$(VALGRIND) $(VGFLAGS) $(TARGET) $(CHKARGS)
	rm -f $(DATAFILE) 

.PHONY: check
check: $(TARGET)
	$(TARGET) $(CHKARGS)

.PHONY: clean
clean:
	rm -f $(TARGET) $(DATAFILE) /mnt/pmem/pm1/PMDKfile.dat /mnt/pmem/pm1/persistFile.bin /mnt/pmem/pm1/persist.bin //mnt/pmem/pm1/tables/*
