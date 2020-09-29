#ifndef MARKING_HPP
#define MARKING_HPP

#include <cstddef>
#include <cstdint>

static const uintptr_t DirtyFlag = 1;
static const uintptr_t PMwCASFlag = 2;
static const uintptr_t RDCSSFlag = 4;
// NOTE: These two flags are normally never set at the same time.
// We get an extra bit out of this, so long as we help complete descriptors
// before marking as migrated. If we didn't do this, then we may need to
// migrate a descriptor, but we wouldn't be able to recognize it as a
// descriptor since it was overwritten with the migration flag.
// TODO: Make sure this flag is handled properly in all cases.
static const uintptr_t MigrationFlag = PMwCASFlag | RDCSSFlag;
// The mask includes all bits not used for flags.
static const uintptr_t AddressMask = ~(DirtyFlag | PMwCASFlag | RDCSSFlag | MigrationFlag);

// Pointer marking.
// Pass in flags to mark different bits.
inline void *setMark(uintptr_t p, size_t flag)
{
    return (void *)(p | flag);
}
inline void *clearMark(uintptr_t p, size_t flag)
{
    return (void *)(p & ~flag);
}
inline void *isMarked(uintptr_t p, size_t flag)
{
    return (void *)(p & flag);
}

#endif