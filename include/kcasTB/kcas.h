#ifndef KCAS_H
#define KCAS_H

#if defined(EMBEDDED_RDCSS_DESC) && defined(KCAS_REUSE_H)
#error Only one of EMBEDDED_RDCSS_DESC and KCAS_REUSE_H can be defined!
#endif

#include <plaf.h>
#include <cstdarg>
#include <csignal>
#include <string.h>
#include <descriptors.h>

#ifdef USE_RECLAIMER_RCU
#include <urcu.h>
#define RECLAIM_RCU_RCUHEAD_DEFN struct rcu_head rcuHeadField
#else
#define RECLAIM_RCU_RCUHEAD_DEFN 
#endif

#define kcastagptr_t uintptr_t
#define kcasptr_t kcasdesc_t<K,NPROC>*
#define rdcsstagptr_t uintptr_t
#define rdcssptr_t rdcssdesc_t*
#define casword_t uintptr_t

#define KCAS_STATE_UNDECIDED 0
#define KCAS_STATE_SUCCEEDED 4
#define KCAS_STATE_FAILED 8

#define KCAS_LEFTSHIFT 2

struct rdcssdesc_t {
#ifdef KCAS_REUSE_H
    volatile mutables_t mutables;
#endif
    casword_t volatile * volatile addr1;
    casword_t volatile old1;
    casword_t volatile * volatile addr2;
    casword_t volatile old2;
    casword_t volatile new2;
    RECLAIM_RCU_RCUHEAD_DEFN;
#ifdef KCAS_REUSE_H
    const static int size = sizeof(mutables)+sizeof(addr1)+sizeof(old1)+sizeof(addr2)+sizeof(old2)+sizeof(new2);
    char padding[PREFETCH_SIZE_BYTES+((64-size%64)%64)]; // add padding to prevent false sharing
#endif
} __attribute__ ((aligned(64)));

struct kcasentry_t { // just part of kcasdesc_t, not a standalone descriptor
    casword_t volatile * volatile addr;
    casword_t volatile oldval;
    casword_t volatile newval;
};

template <int K, int NPROC>
class kcasdesc_t {
public:
#ifdef KCAS_REUSE_H
    volatile mutables_t mutables;
#else
    casword_t state; // mutable to helpers
#endif
    casword_t volatile numEntries;
    kcasentry_t entries[K];
#ifdef EMBEDDED_RDCSS_DESC
    rdcssdesc_t descriptors[NPROC];
#endif
    RECLAIM_RCU_RCUHEAD_DEFN;
#ifdef KCAS_REUSE_H
    const static int size = sizeof(mutables)+sizeof(numEntries)+sizeof(entries);
    char padding[PREFETCH_SIZE_BYTES+((64-size%64)%64)]; // add padding to prevent false sharing
#endif
} __attribute__ ((aligned(64)));

template <int K, int NPROC, class RecManager>
class kcasProvider {
    /**
     * Data definitions
     */
private:
    RecManager * const recmgr;
    
#ifdef KCAS_REUSE_H
    // descriptor reduction algorithm
    #define KCAS_MUTABLES_OFFSET_STATE 0
    #define KCAS_MUTABLES_MASK_STATE 0xf
    #define KCAS_MUTABLES_NEW(mutables) \
        ((((mutables)&MASK_SEQ)+(1<<OFFSET_SEQ)) \
        | (KCAS_STATE_UNDECIDED<<KCAS_MUTABLES_OFFSET_STATE))
    #define RDCSS_MUTABLES_NEW(mutables) \
        (((mutables)&MASK_SEQ)+(1<<OFFSET_SEQ))
    #include "../descriptors/descriptors_impl2.h"
    char __padding_desc[PREFETCH_SIZE_BYTES];
    kcasdesc_t<K,NPROC> kcasDescriptors[LAST_TID+1] __attribute__ ((aligned(64)));
    //char __padding_desc2[PREFETCH_SIZE_BYTES];
    rdcssdesc_t rdcssDescriptors[LAST_TID+1] __attribute__ ((aligned(64)));
    char __padding_desc3[PREFETCH_SIZE_BYTES];
#endif

public:
    static const int FIELD_TYPE_PTR = 0;    // used only by varargs kcas()
    static const int FIELD_TYPE_VALUE = 1;  // used only by varargs kcas()
    debugCounter * cRdcssHelp;
    debugCounter * cKcasHelp;
    
    /**
     * Function declarations
     */
public:
    kcasProvider();
    ~kcasProvider();
    void initThread(const int tid);
    void writePtr(casword_t volatile * addr, casword_t val);
    void writeVal(casword_t volatile * addr, casword_t val);
    casword_t readPtr(const int tid, casword_t volatile * addr);
    casword_t readVal(const int tid, casword_t volatile * addr);
    int kcas(const int tid, const int numEntries, ...);
    int kcas(const int tid, kcasptr_t ptr);
    void debugPrint();
public:
    kcasptr_t allocateKcasDesc(const int tid);
private:
    bool help(const int tid, kcastagptr_t tagptr, kcasptr_t ptr, bool helpingOther);
    void helpOther(const int tid, kcastagptr_t tagptr);
    casword_t rdcssRead(const int tid, casword_t volatile * addr);
    casword_t rdcss(const int tid, rdcssptr_t ptr, rdcsstagptr_t tagptr);
    void rdcssHelp(rdcsstagptr_t tagptr, rdcssptr_t snapshot, bool helpingOther);
    void rdcssHelpOther(rdcsstagptr_t tagptr);
    string tagptrToString(casword_t tagptr);
};

#endif