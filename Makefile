
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

INCLUDES := -I./include
TARGET   := bin/test.bin

.PHONY: default
default: $(TARGET)

$(TARGET): cliff-map/main.cpp cliff-map/main.hpp cliff-map/hashMap.hpp include/define.hpp
	mkdir -p ./bin
	$(CXX) -std=c++17 $(WARNFLAG) -pthread $(OPTFLAG) $(DBGFLAG) $(INCLUDES) $< -o $@

.PHONY: clean
clean:
	rm -f $(TARGET)
