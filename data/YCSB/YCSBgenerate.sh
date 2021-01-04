#!/bin/bash

# This script generates and filters YCSB workloads for use in testing.

PMAPDATA=~/PMap/data
YCSB=~/ycsb-0.17.0
PARAMS=~/PMap/data/YCSB/params

# -P $PARAMS

for alpha in "a" "b" "c" "d" "f"
do
    $YCSB/bin/ycsb.sh load basic -P $YCSB/workloads/workload$alpha -P $PARAMS > $PMAPDATA/YCSB/outputLoad$alpha.txt
    # Remove non-operation lines.
    sed -i '/usertable/!d' $PMAPDATA/YCSB/outputLoad$alpha.txt
    # Filter operation lines to just the operation and key.
    sed -r -i 's/(READ|INSERT|UPDATE|DELETE) usertable user([0-9]{1,20}).*$/\1 \2/gm' $PMAPDATA/YCSB/outputLoad$alpha.txt

    $YCSB/bin/ycsb.sh run basic -P $YCSB/workloads/workload$alpha -P $PARAMS > $PMAPDATA/YCSB/outputRun$alpha.txt
    # Remove non-operation lines.
    sed -i '/usertable/!d' $PMAPDATA/YCSB/outputRun$alpha.txt
    # Filter operation lines to just the operation and key.
    sed -r -i 's/(READ|INSERT|UPDATE|DELETE) usertable user([0-9]{1,20}).*$/\1 \2/gm' $PMAPDATA/YCSB/outputRun$alpha.txt
done