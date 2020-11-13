#!/bin/bash
for i in 1 2 4 8 # {1..8}
do
    make clean && make
    #pmempool create obj --layout="cmap" --size 1G /mnt/pmem/pm1/persistFile.bin
    pmempool create obj --layout="clevel_hash" --size 1G /mnt/pmem/pm1/persistFile.bin
    ./bin/test.out -t $i -n 100000 -p 1 -c 20 -f /mnt/pmem/pm1/persist.bin -r 0 -w 0
done