#!/bin/bash

DATA_STRUCTURES=(ucfDef clevelDef stlDef pmDef onefileDef)
TESTS=(redditTestDef)

for t in "${TESTS[@]}";
do
    for ds in "${DATA_STRUCTURES[@]}";
    do
        if [[ $t = "degreeTestDef" ]]; then
            MAX=4
        elif [[ $t = "redditTestDef" ]]; then
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
            # 2^14 is the initial capacity of level hashing. Match it for other structures here.
            ./bin/test.out -t $i -n 50000 -p 1 -c 14 -f /mnt/pmem/pm1/persist.bin -r 0 -w 0
        done
    done
done
