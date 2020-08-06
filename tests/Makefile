
BUILDTYPE ?= release

ifeq ($(BUILDTYPE),release)
  OPTFLAG ?= -O3 -march=native
  DBGFLAG ?= -DNDEBUG=1
endif

ifeq ($(BUILDTYPE),debug)
  OPTFLAG ?= -O0 -march=native
  DBGFLAG ?= -ggdb
endif

WARNFLAG ?= -Wall -Wextra -pedantic
ARCHFLAG ?= -DDEFAULT_CACHELINE_SIZE=64 # should not be needed for a c++17 compliant compiler

DATAFILE ?= /mnt/pmem/pm1/hashtest.dat
VALGRIND ?= /home/peterp/local/bin/valgrind --tool=pmemcheck
VGFLAGS  ?= --flush-check=yes --flush-align=yes --mult-stores=yes
CHKARGS  ?= -n 1000000 -c 20 -f $(DATAFILE) -t 48 

INCLUDES := -I../include -I../cliff-map -I./onefile
LIBS     := -lpmemobj
TARGET   := ../bin/unique-elems-test.bin

.PHONY: default
default: $(TARGET)

$(TARGET): unique-elems-test.cpp test-onefileMap.hpp ../cliff-map/hashMap.hpp ../include/define.hpp 
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
