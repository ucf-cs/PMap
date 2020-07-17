

OPTFLAG  ?= -O3 -march=native -DNDEBUG=1
WARNFLAG ?= -Wall -Wextra -pedantic
INCLUDES := -I./include

.PHONY: default
default: bin/test.cpp

bin/test.cpp: cliff-map/main.cpp cliff-map/main.hpp cliff-map/hashMap.hpp include/define.hpp
	mkdir -p ./bin
	$(CXX) -std=c++17 $(WARNFLAG) -pthread $(OPTFLAG) $(INCLUDES) $< -o $@
