/* 
 * File:   kcas_reuse.h
 * Author: tabrown
 *
 * Created on June 29, 2016, 5:41 PM
 */

#ifndef KCAS_REUSE_H
#define	KCAS_REUSE_H

#include "kcas.h"
#include <cassert>
#include <stdint.h>
#include <sstream>
using namespace std;

#define BOOL_CAS __sync_bool_compare_and_swap
#define VAL_CAS __sync_val_compare_and_swap

class HPInfo {
public:
    void* obj;
    casword_t* ptrToObj;
    HPInfo(void *_obj, casword_t* _ptrToObj)
            : obj(_obj), ptrToObj(_ptrToObj) {}
};

inline CallbackReturn isValidHP(CallbackArg arg) {
    HPInfo *info = (HPInfo*) arg;
    return ((void*) *info->ptrToObj == info->obj);
}

#define RDCSS_TAGBIT 0x1
#define KCAS_TAGBIT 0x2

static bool isRdcss(casword_t val) {
    return (val & RDCSS_TAGBIT);
}

//static rdcssptr_t unpackRdcssPtr(rdcsstagptr_t tagptr) {
//    return (rdcssptr_t) TAGPTR_UNPACK_PTR(rdcssDescriptors, tagptr);
//}

static bool isKcas(casword_t val) {
    return (val & KCAS_TAGBIT);
}

//template <int K, int NPROC>
//static kcasptr_t unpackKcasPtr(kcastagptr_t tagptr) {
//    return (kcasptr_t) TAGPTR_UNPACK_PTR(kcasDescriptors, tagptr);
//}

// invariant: the descriptor pointed to by ptr is protect()'ed
template <int K, int NPROC, class RecManager>
void kcasProvider<K,NPROC,RecManager>::rdcssHelp(rdcsstagptr_t tagptr, rdcssptr_t snapshot, bool helpingOther) {
    bool readSuccess;
    casword_t v = DESC_READ_FIELD(readSuccess, *snapshot->addr1, snapshot->old1, KCAS_MUTABLES_MASK_STATE, KCAS_MUTABLES_OFFSET_STATE);
    if (!readSuccess) v = KCAS_STATE_SUCCEEDED; // return;
    // this fix is ad-hoc, but we need to say something general about it
    
    if (v == KCAS_STATE_UNDECIDED) { // q here (step 4.5)
        BOOL_CAS(snapshot->addr2, (casword_t) tagptr, snapshot->new2);
    } else {
        // the "fuck it i'm done" action (the same action you'd take if the kcas descriptor hung around indefinitely)
        BOOL_CAS(snapshot->addr2, (casword_t) tagptr, snapshot->old2);
    }
}

template <int K, int NPROC, class RecManager>
void kcasProvider<K,NPROC,RecManager>::rdcssHelpOther(rdcsstagptr_t tagptr) {
    rdcssdesc_t newSnapshot;
    const int sz = rdcssdesc_t::size;
    if (DESC_SNAPSHOT(rdcssdesc_t, rdcssDescriptors, &newSnapshot, tagptr, sz)) {
        rdcssHelp(tagptr, &newSnapshot, true);
    } else {
        //TRACE COUTATOMICTID("helpOther unable to get snapshot of "<<tagptrToString(tagptr)<<endl);
    }
}

// invariant: the descriptor pointed to by ptr is protect()'ed
template <int K, int NPROC, class RecManager>
casword_t kcasProvider<K,NPROC,RecManager>::rdcss(const int tid, rdcssptr_t ptr, rdcsstagptr_t tagptr) {
    casword_t r;
    do {
        r = VAL_CAS(ptr->addr2, ptr->old2, (casword_t) tagptr);
        if (isRdcss(r)) {
            // acquire hp before calling rdcssHelp
            this->cRdcssHelp->inc(tid);
            rdcssHelpOther((rdcsstagptr_t) r);
        }
    } while (isRdcss(r));
    // note: we already hold a hp on ptr (/tagptr) by the invariant
    if (r == ptr->old2) rdcssHelp(tagptr, ptr, false); // finish our own operation
    return r;
}

// acquires and releases its own hazard pointers
template <int K, int NPROC, class RecManager>
casword_t kcasProvider<K,NPROC,RecManager>::rdcssRead(const int tid, casword_t volatile * addr) {
    casword_t r;
    do {
        r = *addr;
//        for (int i=0;i<100;++i) {
//            r = *addr;
//            if (!isRdcss(r)) break;
//        }
        if (isRdcss(r)) {
            // acquire hp before calling rdcssHelp
            this->cRdcssHelp->inc(tid);
            rdcssHelpOther((rdcsstagptr_t) r);
        }
    } while (isRdcss(r));
    return r;
}

template <int K, int NPROC, class RecManager>
kcasProvider<K,NPROC,RecManager>::kcasProvider()
        : recmgr(NULL) {
    cRdcssHelp = new debugCounter(NPROC);
    cKcasHelp = new debugCounter(NPROC);
    DESC_INIT_ALL(kcasDescriptors, KCAS_MUTABLES_NEW, NPROC);
    DESC_INIT_ALL(rdcssDescriptors, RDCSS_MUTABLES_NEW, NPROC);
}

template <int K, int NPROC, class RecManager>
kcasProvider<K,NPROC,RecManager>::~kcasProvider() {
    delete cRdcssHelp;
    delete cKcasHelp;
}

template <int K, int NPROC, class RecManager>
void kcasProvider<K,NPROC,RecManager>::helpOther(const int tid, kcastagptr_t tagptr) {
    kcasdesc_t<K,NPROC> newSnapshot;
    const int sz = kcasdesc_t<K,NPROC>::size;
    //cout<<"size of kcas descriptor is "<<sizeof(kcasdesc_t<K,NPROC>)<<" and sz="<<sz<<endl;
    if (DESC_SNAPSHOT(kcasdesc_t<K comma NPROC>, kcasDescriptors, &newSnapshot, tagptr, sz)) {
        help(tid, tagptr, &newSnapshot, true);
    } else {
        //TRACE COUTATOMICTID("helpOther unable to get snapshot of "<<tagptrToString(tagptr)<<endl);
    }
}

// invariant: the descriptor pointed to by ptr is protect()'ed
template <int K, int NPROC, class RecManager>
bool kcasProvider<K,NPROC,RecManager>::help(const int tid, kcastagptr_t tagptr, kcasptr_t snapshot, bool helpingOther) {
    TRACE COUTATOMICTID("help tagptr="<<tagptrToString(tagptr)<<" helpingOther="<<helpingOther<<endl);
    
    // phase 1: "locking" addresses for this kcas
    int newstate;
    
    // read state field
    kcasptr_t ptr = TAGPTR_UNPACK_PTR(kcasDescriptors, tagptr);
    bool successBit;
    int state = DESC_READ_FIELD(successBit, ptr->mutables, tagptr, KCAS_MUTABLES_MASK_STATE, KCAS_MUTABLES_OFFSET_STATE);
    if (!successBit) {
        //cout<<"failed to read state field "<<tagptrToString(tagptr)<<endl;
        assert(helpingOther);
        return false;
    }
    
    if (state == KCAS_STATE_UNDECIDED) {
        newstate = KCAS_STATE_SUCCEEDED;
        for (int i = helpingOther; i < snapshot->numEntries; i++) {
retry_entry:
            // prepare rdcss descriptor and run rdcss
            rdcssdesc_t *rdcssptr = DESC_NEW(rdcssDescriptors, RDCSS_MUTABLES_NEW, tid);
            rdcssptr->addr1 = (casword_t*) &ptr->mutables;
            rdcssptr->old1 = tagptr; // pass the sequence number (as part of tagptr)
            rdcssptr->old2 = snapshot->entries[i].oldval;
            rdcssptr->addr2 = snapshot->entries[i].addr; // p stopped here (step 2)
            rdcssptr->new2 = (casword_t) tagptr;
            DESC_INITIALIZED(rdcssDescriptors, tid);
            
            casword_t val;
            val = rdcss(tid, rdcssptr, TAGPTR_NEW(tid, rdcssptr->mutables, RDCSS_TAGBIT));
            
            // check for failure of rdcss and handle it
            if (isKcas(val)) {
                // if rdcss failed because of a /different/ kcas, we help it
                if (val != (casword_t) tagptr) {
                    this->cKcasHelp->inc(tid);
                    helpOther(tid, (kcastagptr_t) val);
                    goto retry_entry;
                }
            } else {
                if (val != snapshot->entries[i].oldval) {
                    newstate = KCAS_STATE_FAILED;
                    break;
                }
            }
        }
//        MUTABLES_WRITE_FIELD(ptr->mutables, snapshot->mutables, newstate, KCAS_MUTABLES_MASK_STATE, KCAS_MUTABLES_OFFSET_STATE);
        MUTABLES_BOOL_CAS_FIELD(successBit
                , ptr->mutables, snapshot->mutables
                , KCAS_STATE_UNDECIDED, newstate
                , KCAS_MUTABLES_MASK_STATE, KCAS_MUTABLES_OFFSET_STATE);
    }

    // phase 2 (all addresses are now "locked" for this kcas)
    state = DESC_READ_FIELD(successBit, ptr->mutables, tagptr, KCAS_MUTABLES_MASK_STATE, KCAS_MUTABLES_OFFSET_STATE);
    if (!successBit) return false;

    bool succeeded = (state == KCAS_STATE_SUCCEEDED);
    for (int i = 0; i < snapshot->numEntries; i++) {
        casword_t newval = succeeded ? snapshot->entries[i].newval : snapshot->entries[i].oldval;
        BOOL_CAS(snapshot->entries[i].addr, (casword_t) tagptr, newval);
    }
    return succeeded;
}

// TODO: replace crappy bubblesort with something fast for large K
// (maybe even use insertion sort for small K)
template <int K, int NPROC>
static void kcasdesc_sort(kcasptr_t ptr) {
    kcasentry_t temp;
    for (int i = 0; i < ptr->numEntries; i++) {
        for (int j = 0; j < ptr->numEntries - i - 1; j++) {
            if (ptr->entries[j].addr > ptr->entries[j + 1].addr) {
                temp = ptr->entries[j];
                ptr->entries[j] = ptr->entries[j + 1];
                ptr->entries[j + 1] = temp;
            }
        }
    }
}

template <int K, int NPROC, class RecManager>
int kcasProvider<K,NPROC,RecManager>::kcas(const int tid, kcasptr_t ptr) {
    // sort entries in the kcas descriptor to guarantee progress
    kcasdesc_sort<K,NPROC>(ptr);
    DESC_INITIALIZED(kcasDescriptors, tid);
    kcastagptr_t tagptr = TAGPTR_NEW(tid, ptr->mutables, KCAS_TAGBIT);

    // perform the kcas and retire the old descriptor
    bool result = help(tid, tagptr, ptr, false);
    return result;
}

template <int K, int NPROC, class RecManager>
casword_t kcasProvider<K,NPROC,RecManager>::readPtr(const int tid, casword_t volatile * addr) {
    casword_t r;
    do {
        r = rdcssRead(tid, addr);
//        for (int i=0;i<100;++i) {
//            r = rdcssRead(tid, addr);
//            if (!isKcas(r)) break;
//        }
        if (isKcas(r)) {
            this->cKcasHelp->inc(tid);
            helpOther(tid, (kcastagptr_t) r);
        }
    } while (isKcas(r));
    return r;
}

template <int K, int NPROC, class RecManager>
casword_t kcasProvider<K,NPROC,RecManager>::readVal(const int tid, casword_t volatile * addr) {
    return ((casword_t) readPtr(tid, addr))>>KCAS_LEFTSHIFT;
}

template <int K, int NPROC, class RecManager>
void kcasProvider<K,NPROC,RecManager>::writePtr(casword_t volatile * addr, casword_t ptr) {
    *addr = ptr;
    // note: enter/leaveQstate calls are not needed here,
    // because the writePtr/Val functions do not access fields of descriptors!
}

template <int K, int NPROC, class RecManager>
void kcasProvider<K,NPROC,RecManager>::writeVal(casword_t volatile * addr, casword_t val) {
    writePtr(addr, val<<KCAS_LEFTSHIFT);
    // note: enter/leaveQstate calls are not needed here,
    // because the writePtr/Val functions do not access fields of descriptors!
}

template <int K, int NPROC, class RecManager>
void kcasProvider<K,NPROC,RecManager>::initThread(const int tid) {
}

template <int K, int NPROC, class RecManager>
void kcasProvider<K,NPROC,RecManager>::debugPrint() {
    cout<<"rdcss helping : "<<this->cRdcssHelp->getTotal()<<endl;
    cout<<"kcas helping  : "<<this->cKcasHelp->getTotal()<<endl;
}

template <int K, int NPROC, class RecManager>
kcasptr_t kcasProvider<K,NPROC,RecManager>::allocateKcasDesc(const int tid) {
    // allocate a new kcas descriptor
    kcasptr_t ptr = DESC_NEW(kcasDescriptors, KCAS_MUTABLES_NEW, tid);
    return ptr;
}

template <int K, int NPROC, class RecManager>
string kcasProvider<K,NPROC,RecManager>::tagptrToString(uintptr_t tagptr) {
    stringstream ss;
    if (tagptr) {
        kcasptr_t ptr;
        ss<<"<seq="<<UNPACK_SEQ(tagptr)<<",tid="<<TAGPTR_UNPACK_TID(tagptr)<<">";
        ptr = TAGPTR_UNPACK_PTR(kcasDescriptors, tagptr);

        // print contents of actual scx record
        intptr_t mutables = ptr->mutables;
        ss<<"[";
        ss<<"state="<<MUTABLES_UNPACK_FIELD(mutables, KCAS_MUTABLES_MASK_STATE, KCAS_MUTABLES_OFFSET_STATE);
        ss<<",";
        ss<<"seq="<<UNPACK_SEQ(mutables);
        ss<<"]";
    } else {
        ss<<"null";
    }
    return ss.str();
}

void deinitThread(const int tid) {
}

#endif	/* KCAS_REUSE_H */
