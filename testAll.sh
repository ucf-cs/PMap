#!/bin/bash

DATA_STRUCTURES=(ucfDef stlDef pmDef onefileDef clevelDef)
TESTS=(alternatingTestDef degreeTestDef randomTestDef)

for t in "${TESTS[@]}";
do
    for ds in "${DATA_STRUCTURES[@]}";
    do
        if [[ $ds = "degreeTestDef" ]]; then
            MAX=4
        elif [[ $ds = "redditTestDef" ]]; then
            MAX=1
        else
            MAX=80
        fi
        for ((i=1;i<=MAX;i++));
        do
            make clean && make DEFINES="-D$ds -D$t"
            if [[ $ds = "pmDef" ]]; then
                pmempool create obj --layout="cmap" --size 1G /mnt/pmem/pm1/persistFile.bin
            fi
            if [[ $ds = "clevelDef" ]]; then
                pmempool create obj --layout="clevel_hash" --size 1G /mnt/pmem/pm1/persistFile.bin
            fi
            ./bin/test.out -t $i -n 400000 -p 1 -c 22 -f /mnt/pmem/pm1/persist.bin -r 0 -w 0
        done
    done
done