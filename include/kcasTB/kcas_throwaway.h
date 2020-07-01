// TODO: add variant where rdcss descriptors are allocated (non embedded)
// but are also reused for different iterations of the loop
// TODO: add proper HPs to all variants (remember to retire rdcss 
// descriptors explicitly whenever they are NOT embedded in kcas descriptors!)

#ifndef KCAS_IMPL_H
#define KCAS_IMPL_H

#include "kcas.h"
#include <iostream>
#include <cassert>
#include <stdint.h>
using namespace std;

#define BOOL_CAS __sync_bool_compare_and_swap
#define VAL_CAS __sync_val_compare_and_swap

class HPInfo {
public:
    void * obj;
    casword_t volatile * ptrToObj;
    HPInfo(void *_obj, casword_t volatile * _ptrToObj)
            : obj(_obj), ptrToObj(_ptrToObj) {}
};

inline CallbackReturn isValidHP(CallbackArg arg) {
    HPInfo *info = (HPInfo*) arg;
    return ((void*) *info->ptrToObj == info->obj);
}

static bool isRdcss(casword_t val) {
    return (val & 0x1);
}

static rdcsstagptr_t getRdcssTagged(rdcssptr_t ptr) {
    return (rdcsstagptr_t) ((casword_t) ptr | 0x1);
}

static rdcssptr_t unpackRdcssPtr(rdcsstagptr_t tagptr) {
    return (rdcssptr_t) ((casword_t) tagptr & ~3);
}

static bool isKcas(casword_t val) {
    return (val & 0x2);
}

template <int K, int NPROC>
static kcastagptr_t getKcasTagged(kcasptr_t ptr) {
    return (kcastagptr_t) ((casword_t) ptr | 0x2);
}

template <int K, int NPROC>
static kcasptr_t unpackKcasPtr(kcastagptr_t tagptr) {
    return (kcasptr_t) ((casword_t) tagptr & ~3);
}

// invariant: the descriptor pointed to by ptr is protect()'ed
template <int K, int NPROC, class RecManager>
void kcasProvider<K,NPROC,RecManager>::rdcssHelp(rdcsstagptr_t tagptr, rdcssptr_t unused, bool helpingOther) {
    rdcssdesc_t* ptr = unpackRdcssPtr(tagptr); // q here (step 3)
    casword_t v = *(ptr->addr1); 
    if (v == ptr->old1) { // q here (step 4.5)
        BOOL_CAS(ptr->addr2, (casword_t) tagptr, ptr->new2);
    } else {
        BOOL_CAS(ptr->addr2, (casword_t) tagptr, ptr->old2); // read these fields in step 4.5
    }
}

// invariant: the descriptor pointed to by ptr is protect()'ed
template <int K, int NPROC, class RecManager>
casword_t kcasProvider<K,NPROC,RecManager>::rdcss(const int tid, rdcssptr_t ptr, rdcsstagptr_t tagptr) {
    casword_t r;
    do {
        r = VAL_CAS(ptr->addr2, ptr->old2, (casword_t) tagptr); // p here (step 5)
        if (isRdcss(r)) {
            // acquire hp before calling rdcssHelp
            rdcssptr_t otherRdcssptr = unpackRdcssPtr(r);
            HPInfo info ((void*) r, ptr->addr2);
            if (recmgr->protect(tid, otherRdcssptr, isValidHP, &info, true)) {
                // if we fail to acquire the HP, we simply don't help
                this->cRdcssHelp->inc(tid);
                rdcssHelp((rdcsstagptr_t) r, NULL, true);
                recmgr->unprotect(tid, otherRdcssptr);
            }
        }
    } while (isRdcss(r));
    // note: we already hold a hp on ptr (/tagptr) by the invariant
    if (r == ptr->old2) rdcssHelp(tagptr, NULL, false); // finish our own operation
    return r;
}

// acquires and releases its own hazard pointers
template <int K, int NPROC, class RecManager>
casword_t kcasProvider<K,NPROC,RecManager>::rdcssRead(const int tid, casword_t volatile * addr) {
    casword_t r;
    do {
        r = *addr;
        if (isRdcss(r)) {
            // acquire hp before calling rdcssHelp
            rdcssptr_t otherRdcssptr = unpackRdcssPtr(r);
            HPInfo info ((void*) r, addr);
            if (recmgr->protect(tid, otherRdcssptr, isValidHP, &info, true)) {
                // if we fail to acquire the HP, we simply don't help
                this->cRdcssHelp->inc(tid);
                rdcssHelp((rdcsstagptr_t) r, NULL, true);
                recmgr->unprotect(tid, otherRdcssptr);
            }
        }
    } while (isRdcss(r));
    return r;
}

template <int K, int NPROC, class RecManager>
kcasProvider<K,NPROC,RecManager>::kcasProvider()
        : recmgr(new RecManager(NPROC, SIGQUIT)) {
    cRdcssHelp = new debugCounter(NPROC);
    cKcasHelp = new debugCounter(NPROC);
}

template <int K, int NPROC, class RecManager>
kcasProvider<K,NPROC,RecManager>::~kcasProvider() {
    delete cRdcssHelp;
    delete cKcasHelp;
    delete recmgr;
}

// invariant: the descriptor pointed to by ptr is protect()'ed
template <int K, int NPROC, class RecManager>
bool kcasProvider<K,NPROC,RecManager>::help(const int tid, kcastagptr_t tagptr, kcasptr_t ptr, bool helpingOther) {
    // phase 1: "locking" addresses for this kcas
    int newstate;
    if (ptr->state == KCAS_STATE_UNDECIDED) {
        newstate = KCAS_STATE_SUCCEEDED;
        for (int i = helpingOther; i < ptr->numEntries; i++) {
retry_entry:
            // prepare rdcss descriptor and run rdcss
#ifdef EMBEDDED_RDCSS_DESC
            rdcssdesc_t *rdcssptr = &ptr->descriptors[tid];
#else
            rdcssdesc_t *rdcssptr = recmgr->template allocate<rdcssdesc_t>(tid);
            if (ptr == NULL) {
                cout<<"ERROR: FAILED TO ALLOCATE MEMORY"<<endl;
                exit(-1);
            }
#endif
            rdcssptr->addr1 = &ptr->state;
            rdcssptr->old1 = KCAS_STATE_UNDECIDED;
            // THE ORDER OF THE NEXT TWO LINES MATTERS
            rdcssptr->old2 = ptr->entries[i].oldval;
            rdcssptr->addr2 = ptr->entries[i].addr; // p stopped here (step 2)
            rdcssptr->new2 = (casword_t) tagptr;
            
            // note: no need to acquire hp, since we already have a hp to
            //       the parent kcas descriptor
            casword_t val = rdcss(tid, rdcssptr, getRdcssTagged(rdcssptr));
#ifdef EMBEDDED_RDCSS_DESC
#else
            recmgr->retire(tid, rdcssptr);
#endif
            
            // check for failure of rdcss and handle it
            if (isKcas(val)) {
                // if rdcss failed because of a /different/ kcas, we help it
                if (val != (casword_t) tagptr) {
                    kcasptr_t otherKcasptr = unpackKcasPtr<K,NPROC>((kcastagptr_t) val);
                    // acquire HP in order to help
                    HPInfo info ((void*) val, ptr->entries[i].addr);
                    if (recmgr->protect(tid, otherKcasptr, isValidHP, &info, true)) {
                        // if we fail to acquire the HP, we simply don't help
                        this->cKcasHelp->inc(tid);
                        help(tid, (kcastagptr_t) val, otherKcasptr, true);
                        recmgr->unprotect(tid, otherKcasptr);
                    }
                    goto retry_entry;
                }
            } else {
                if (val != ptr->entries[i].oldval) {
                    newstate = KCAS_STATE_FAILED;
                    break;
                }
            }
        }
        BOOL_CAS(&(ptr->state), KCAS_STATE_UNDECIDED, newstate);
    }

    // phase 2 (all addresses are now "locked" for this kcas)
    bool succeeded = (ptr->state == KCAS_STATE_SUCCEEDED);
    for (int i = 0; i < ptr->numEntries; i++) {
        casword_t newval = succeeded ? ptr->entries[i].newval : ptr->entries[i].oldval;
        BOOL_CAS(ptr->entries[i].addr, (casword_t) tagptr, newval);
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

//template <int K, int NPROC, class RecManager>
//int kcasProvider<K,NPROC,RecManager>::kcas(const int tid, const int numEntries, ...) {
//    // allocate a new kcas descriptor
//    kcasptr_t ptr = allocateKcasDesc(tid);
//    kcastagptr_t tagptr = getKcasTagged<K,NPROC>(ptr);
//    
//    // initialize the new kcas descriptor
//    ptr->state = KCAS_STATE_UNDECIDED;
//    ptr->numEntries = numEntries;
//
//    va_list args;
//    va_start(args, numEntries); // note: second arg is last param before ellipsis
//    for (int i=0;i<numEntries;++i) {
//        casword_t* addr = va_arg(args, casword_t*);
//        casword_t oldval = va_arg(args, casword_t);
//        casword_t newval = va_arg(args, casword_t);
//        casword_t fieldType = va_arg(args, casword_t);
//        assert(fieldType == FIELD_TYPE_VALUE || fieldType == FIELD_TYPE_PTR);
//        // note: the following bit-math relies on the fact that there are only two
//        // field types, and pointers, which do not need to be shifted since they
//        // have 2 or 3 free low-order bits, have field type 0, and other values,
//        // which we assume must be shifted (to allow room for tagging with bits
//        // that say "i am an rdcss/kcas descriptor and not a value"), have
//        // field type 1.
//        ptr->entries[i].addr = addr;
//        ptr->entries[i].oldval = oldval<<(fieldType*KCAS_LEFTSHIFT);
//        ptr->entries[i].newval = newval<<(fieldType*KCAS_LEFTSHIFT);
//    }
//    va_end(args);
//
//    // sort entries in the kcas descriptor to guarantee progress
//    kcasdesc_sort<K,NPROC>(ptr);
//
//    // perform the kcas and retire the old descriptor
//    recmgr->leaveQuiescentState(tid); // only needed for EBR
//    recmgr->protect(tid, ptr, callbackReturnTrue, NULL, false);
//    bool result = help(tid, tagptr, ptr, false);
//    recmgr->enterQuiescentState(tid); // only needed for EBR
//    recmgr->retire(tid, ptr);
//    return result;
//}

template <int K, int NPROC, class RecManager>
int kcasProvider<K,NPROC,RecManager>::kcas(const int tid, kcasptr_t ptr) {
    kcastagptr_t tagptr = getKcasTagged<K,NPROC>(ptr);
    
    // initialize the new kcas descriptor
    ptr->state = KCAS_STATE_UNDECIDED;

    // sort entries in the kcas descriptor to guarantee progress
    kcasdesc_sort<K,NPROC>(ptr);

    // perform the kcas and retire the old descriptor
    recmgr->leaveQuiescentState(tid); // only needed for EBR
    recmgr->protect(tid, ptr, callbackReturnTrue, NULL, false);
    bool result = help(tid, tagptr, ptr, false);
    recmgr->enterQuiescentState(tid); // only needed for EBR
    recmgr->retire(tid, ptr);
    return result;
}

template <int K, int NPROC, class RecManager>
casword_t kcasProvider<K,NPROC,RecManager>::readPtr(const int tid, casword_t volatile * addr) {
    casword_t r;
    do {
        recmgr->leaveQuiescentState(tid); // only needed for EBR
        r = rdcssRead(tid, addr); // acquires and releases its own hps
        if (isKcas(r)) {
            // acquire HP on the kcas descriptor (in order to help it)
            kcasptr_t otherKcasptr = unpackKcasPtr<K,NPROC>((kcastagptr_t) r);
            HPInfo info (otherKcasptr, addr);
            if (recmgr->protect(tid, otherKcasptr, isValidHP, &info, true)) {
                // if we fail to acquire the HP, we simply don't help
                this->cKcasHelp->inc(tid);
                help(tid, (kcastagptr_t) r, otherKcasptr, true);
                recmgr->unprotect(tid, otherKcasptr);
            }
        }
        recmgr->enterQuiescentState(tid); // only needed for EBR
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
    recmgr->initThread(tid);
}

template <int K, int NPROC, class RecManager>
void kcasProvider<K,NPROC,RecManager>::debugPrint() {
    cout<<"rdcss helping : "<<this->cRdcssHelp->getTotal()<<endl;
    cout<<"kcas helping  : "<<this->cKcasHelp->getTotal()<<endl;
    recmgr->printStatus();
}

template <int K, int NPROC, class RecManager>
kcasptr_t kcasProvider<K,NPROC,RecManager>::allocateKcasDesc(const int tid) {
    // allocate a new kcas descriptor
    kcasptr_t ptr = recmgr->template allocate< kcasdesc_t<K,NPROC> >(tid);
    if (ptr == NULL) {
        cout<<"ERROR: FAILED TO ALLOCATE MEMORY"<<endl;
        exit(-1);
    }
    return ptr;
}

void deinitThread(const int tid) {
}

#endif
