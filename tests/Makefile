
BUILDTYPE ?= debug

ifeq ($(BUILDTYPE),release)
  OPTFLAG ?= -O3 -march=native
  DBGFLAG ?= -DNDEBUG=1
endif

ifeq ($(BUILDTYPE),debug)
  OPTFLAG ?= -O0 -g2 -march=native
  DBGFLAG ?= -ggdb
endif

WARNFLAG ?= -Wall -Wextra -pedantic
ARCHFLAG ?= -DDEFAULT_CACHELINE_SIZE=64 # should not be needed for a c++17 compliant compiler

DATAFILE ?= /mnt/pmem/pm1/hashtest.dat
VALGRIND ?= /home/marioman/local/bin/valgrind --tool=pmemcheck
VGFLAGS  ?= --flush-check=yes --flush-align=yes --mult-stores=yes
CHKARGS  ?= -n 1000000 -c 20 -f $(DATAFILE) -t 48 

INCLUDES := -I../include -I../cliff-map -I/home/marioman/onefile -I/home/marioman/usr/include #-I/home/marioman/local/include
LIBS     := -L/home/marioman/local/lib -lpmemobj -L/home/marioman/usr/lib #-L/usr/lib/x86_64-linux-gnu
TARGET   := ../bin/test-unique-elems.bin
HEADERS  := container-onefileMap.hpp container-pmemMap.hpp container-ucfMap.hpp container-stlMap.hpp

.PHONY: default
default: $(TARGET)

$(TARGET): test-framework.cpp $(HEADERS) ../cliff-map/hashMap.hpp ../include/define.hpp 
	mkdir -p ../bin
	$(CXX) -std=c++17 $(WARNFLAG) -pthread $(OPTFLAG) $(DBGFLAG) $(ARCHFLAG) $(INCLUDES) $(LIBS) $< -o $@

.PHONY: valcheck
valcheck: $(TARGET)
	$(VALGRIND) $(VGFLAGS) $(TARGET) $(CHKARGS)
	rm -f $(DATAFILE) 

.PHONY: check
check: $(TARGET)
	$(TARGET) $(CHKARGS)
#rm -f $(DATAFILE) /mnt/pmem/pm1/PFMKtests

.PHONY: clean
clean:
	rm -f $(TARGET) $(DATAFILE) /mnt/pmem/pm1/PFMKtests
