#!/bin/bash
for i in {1..8}
do
    make clean && make
    #pmempool create obj --layout="cmap" --size 1G /home/marioman/PMap/data/persistFile.bin.pool
    pmempool create obj --layout="clevel_hash" --size 1G /home/marioman/PMap/data/persistFile.bin.pool
    ./bin/test.out -t $i -n 1000000 -p 1 -c 20 -f ./data/persistFile.bin -r 0 -w 0
    #./bin/test.out -t $i -n 1000 -p 1 -c 20 -f ./data/persistFile.bin -r 0 -w 0
done