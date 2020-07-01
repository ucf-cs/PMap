/* 
 * File:   test_kcas.cpp
 * Author: tabrown
 *
 * Created on June 23, 2016, 5:01 PM
 */

#include <cstdlib>
#include <iostream>
#include "kcas_throwaway.h"
#include "../recordmgr/record_manager.h"
using namespace std;

#define KCAS_MAXK 8
#define KCAS_MAXTHREADS 64
typedef kcasdesc_t<KCAS_MAXK, KCAS_MAXTHREADS> DescriptorType;

typedef reclaimer_hazardptr<casword_t> Reclaim;
//typedef reclaimer_none<casword_t> Reclaim;

typedef allocator_new<casword_t> Alloc;
typedef pool_perthread_and_shared<casword_t> Pool;
typedef record_manager<Reclaim, Alloc, Pool, DescriptorType> RecManagerType;

#define READ(addr) prov.readVal(tid, &(addr))

int main(int argc, char** argv) {
    const int tid = 0;

    kcasProvider<KCAS_MAXK,KCAS_MAXTHREADS,RecManagerType> prov;
    casword_t arr[100];
    for (int i=0;i<100;++i) {
        prov.writeVal(&arr[i], 0);
    }

    casword_t fieldType = kcasProvider<KCAS_MAXK,KCAS_MAXTHREADS,RecManagerType>::FIELD_TYPE_VALUE;
    bool result = prov.kcas(tid, 3
            , &arr[0], READ(arr[0]), READ(arr[0])+1, fieldType
            , &arr[1], READ(arr[1]), READ(arr[1])+1, fieldType
            , &arr[2], READ(arr[2]), READ(arr[2])+1, fieldType
    );
    
    cout<<endl;
    cout<<"result="<<result<<" arr[0]="<<READ(arr[0])<<" arr[1]="<<READ(arr[1])<<" arr[2]="<<READ(arr[2])<<" arr[3]="<<READ(arr[3])<<endl;
    prov.debugPrint();
    cout<<endl;
    
    return 0;
}

