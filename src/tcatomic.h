/*==========================================================*/
/*							ATOMICS							*/
/*==========================================================*/

#pragma once
#include "tccore.h"
#include "tcos.h"
#include "tctypes.h"


typedef enum {
    MEMORY_RELAXED,
    MEMORY_ACQUIRE,
    MEMORY_RELEASE,
    MEMORY_ACQ_REL
} MemoryOrder;

#define CAS atomic_compare_exchange_strong

#if COMPILER_MSVC
#include <intrin.h>

/* Atomic types */
typedef struct { uint32_t _nonatomic; } atomic32_t;
typedef struct { uint64_t _nonatomic; } atomic64_t;
typedef struct { size_t _nonatomic; } atomic_t;

/* Atomic operations: */
#define signal_fence_consume() (0)
#define signal_fence_acquire() _ReadWriteBarrier()
#define signal_fence_release() _ReadWriteBarrier()
#define signal_fence_seq_cst() _ReadWriteBarrier()

#define thread_fence_consume() (0)
#define thread_fence_acquire() _ReadWriteBarrier()
#define thread_fence_release() _ReadWriteBarrier()
#define thread_fence_seq_cst() MemoryBarrier()

/* 32-bit atomic operations: */
static INLINE uint32_t load_32_relaxed(const atomic32_t* object) { return object->_nonatomic; }
static INLINE void store_32_relaxed(atomic32_t* object, uint32_t value) { object->_nonatomic = value; }
static INLINE uint32_t exchange_32_relaxed(atomic32_t* object, uint32_t desired) { return _InterlockedExchange((long*)object, desired); }
static INLINE uint32_t fetch_add_32_relaxed(atomic32_t* object, int32_t operand) { return _InterlockedExchangeAdd((long*)object, operand); }
static INLINE uint32_t fetch_and_32_relaxed(atomic32_t* object, uint32_t operand) { return _InterlockedAnd((long*)object, operand); }
static INLINE uint32_t fetch_or_32_relaxed(atomic32_t* object, uint32_t operand) { return _InterlockedOr((long*)object, operand); }
static INLINE uint32_t compare_exchange_32_relaxed(atomic32_t* object, uint32_t expected, uint32_t desired) { return _InterlockedCompareExchange((long*)object, desired, expected); }
static INLINE intptr_t compare_exchange_weak_32_relaxed(atomic32_t* object, uint32_t* expected, uint32_t desired) { 
    uint32_t e = *expected;
    uint32_t previous = _InterlockedCompareExchange((long*)object, desired, e);
    intptr_t matched = (previous == e);
    if (!matched) *expected = previous;
    return matched;
}

/* 64-bit atomic operations: */
static INLINE uint64_t load_64_relaxed(const atomic64_t* object) {
#if CPU_X64
    return object->_nonatomic;
#else
    uint64_t result;
    __asm {
        mov esi, object;
        mov ebx, eax;
        mov ecx, edx;
        lock cmpxchg8b[esi];
        mov dword ptr result, eax;
        mov dword ptr result[4], edx;
    }
    return result;
#endif
}
static INLINE void store_64_relaxed(atomic64_t* object, uint64_t value) {
#if CPU_X64
    object->_nonatomic = value;
#else
    __asm {
        mov esi, object;
        mov ebx, dword ptr value;
        mov ecx, dword ptr value[4];
    retry:
        cmpxchg8b[esi];
        jne retry;
    }
#endif
}
static INLINE uint64_t exchange_64_relaxed(atomic64_t* object, uint64_t desired) {
#if CPU_X64
    return _InterlockedExchange64((LONGLONG*)object, desired);
#else
    uint64_t expected = object->_nonatomic;
    for (;;) {
        uint64_t original = _InterlockedCompareExchange64((LONGLONG*)object, desired, expected);
        if (original == expected) return original;
        expected = original;
    }
#endif
}
static INLINE uint64_t fetch_add_64_relaxed(atomic64_t* object, int64_t operand) {
#if CPU_X64
    return _InterlockedExchangeAdd64((LONGLONG*)object, operand);
#else
    uint64_t expected = object->_nonatomic;
    for (;;) {
        uint64_t original = _InterlockedCompareExchange64((LONGLONG*)object, expected + operand, expected);
        if (original == expected) return original;
        expected = original;
    }
#endif
}
static INLINE uint64_t fetch_and_64_relaxed(atomic64_t* object, uint64_t operand) {
#if CPU_X64
    return _InterlockedAnd64((LONGLONG*)object, operand);
#else
    uint64_t expected = object->_nonatomic;
    for (;;) {
        uint64_t original = _InterlockedCompareExchange64((LONGLONG*)object, expected & operand, expected);
        if (original == expected) return original;
        expected = original;
    }
#endif
}
static INLINE uint64_t fetch_or_64_relaxed(atomic64_t* object, uint64_t operand) {
#if CPU_X64
    return _InterlockedOr64((LONGLONG*)object, operand);
#else
    uint64_t expected = object->_nonatomic;
    for (;;) {
        uint64_t original = _InterlockedCompareExchange64((LONGLONG*)object, expected | operand, expected);
        if (original == expected) return original;
        expected = original;
    }
#endif
}
static INLINE intptr_t compare_exchange_weak_64_relaxed(atomic64_t* object, uint64_t* expected, uint64_t desired) {
    uint64_t e = *expected;
    uint64_t previous = _InterlockedCompareExchange64((LONGLONG*)object, desired, e);
    intptr_t matched = (previous == e);
    if (!matched) *expected = previous;
    return matched;
}
static INLINE uint64_t compare_exchange_64_relaxed(atomic64_t* object, uint64_t expected, uint64_t desired) { return _InterlockedCompareExchange64((LONGLONG*)object, desired, expected); }

#elif COMPILER_GCC && (CPU_X86 || CPU_X64)

/* Atomic types */
typedef struct { volatile uint32_t _nonatomic; } __attribute__((aligned(4))) atomic32_t;
typedef struct { volatile uint64_t _nonatomic; } __attribute__((aligned(8))) atomic64_t;
typedef struct { void* volatile _nonatomic; } __attribute__((aligned(PTR_SIZE))) atomic_t;

/* Atomic operations: */
#define signal_fence_consume() (0)
#define signal_fence_acquire() asm volatile("" ::: "memory")
#define signal_fence_release() asm volatile("" ::: "memory")
#define signal_fence_seq_cst() asm volatile("" ::: "memory")

#define thread_fence_consume() (0)
#define thread_fence_acquire() asm volatile("" ::: "memory")
#define thread_fence_release() asm volatile("" ::: "memory")
#if CPU_X64
#define thread_fence_seq_cst() asm volatile("lock; orl $0, (%%rsp)" ::: "memory")
#else
#define thread_fence_seq_cst() asm volatile("lock; orl $0, (%%esp)" ::: "memory")
#endif

/* 32-bit atomic operations: */
static INLINE uint32_t load_32_relaxed(const atomic32_t* object) { return object->_nonatomic; }
static INLINE void store_32_relaxed(atomic32_t* object, uint32_t desired) { object->_nonatomic = desired; }
static INLINE uint32_t compare_exchange_32_relaxed(atomic32_t* object, uint32_t expected, uint32_t desired) {
    uint32_t previous;
    asm volatile("lock; cmpxchgl %2, %1" : "=a"(previous), "+m"(object->_nonatomic) : "q"(desired), "0"(expected));
    return previous;
}
static INLINE intptr_t compare_exchange_weak_32_relaxed(atomic32_t* object, uint32_t* expected, uint32_t desired) {
    uint32_t e = *expected;
    uint32_t previous;
    intptr_t matched;
    asm volatile("lock; cmpxchgl %2, %1" : "=a"(previous), "+m"(object->_nonatomic) : "q"(desired), "0"(e));
    matched = (previous == e);
    if (!matched) *expected = previous;
    return matched;
}
static INLINE uint32_t exchange_32_relaxed(atomic32_t* object, uint32_t desired) {
    uint32_t previous;
    asm volatile("xchgl %0, %1" : "=r"(previous), "+m"(object->_nonatomic) : "0"(desired));
    return previous;
}
static INLINE uint32_t fetch_add_32_relaxed(atomic32_t* object, int32_t operand) {
    uint32_t original;
    asm volatile("lock; xaddl %0, %1"
        : "=r"(original), "+m"(object->_nonatomic)
        : "0"(operand));
    return original;
}
static INLINE uint32_t fetch_and_32_relaxed(atomic32_t* object, uint32_t operand) {
    uint32_t original;
    register uint32_t temp;
    asm volatile("1:     movl    %1, %0\n"
        "       movl    %0, %2\n"
        "       andl    %3, %2\n"
        "       lock; cmpxchgl %2, %1\n"
        "       jne     1b"
        : "=&a"(original), "+m"(object->_nonatomic), "=&r"(temp)
        : "r"(operand));
    return original;
}
static INLINE uint32_t fetch_or_32_relaxed(atomic32_t* object, uint32_t operand) {
    uint32_t original;
    register uint32_t temp;
    asm volatile("1:     movl    %1, %0\n"
        "       movl    %0, %2\n"
        "       orl     %3, %2\n"
        "       lock; cmpxchgl %2, %1\n"
        "       jne     1b"
        : "=&a"(original), "+m"(object->_nonatomic), "=&r"(temp)
        : "r"(operand));
    return original;
}

/* 64-bit atomic operations: */
#if CPU_X64
static INLINE uint64_t load_64_relaxed(const atomic64_t* object) { return object->_nonatomic; }
static INLINE void store_64_relaxed(atomic64_t* object, uint64_t desired) { object->_nonatomic = desired; }
static INLINE uint64_t compare_exchange_64_relaxed(atomic64_t* object, uint64_t expected, uint64_t desired) {
    uint64_t previous;
    asm volatile("lock; cmpxchgq %2, %1" : "=a"(previous), "+m"(object->_nonatomic) : "q"(desired), "0"(expected));
    return previous;
}
static INLINE intptr_t compare_exchange_weak_64_relaxed(atomic64_t* object, uint64_t* expected, uint64_t desired) {
    uint64_t e = *expected;
    uint64_t previous;
    intptr_t matched;
    asm volatile("lock; cmpxchgq %2, %1" : "=a"(previous), "+m"(object->_nonatomic) : "q"(desired), "0"(e));
    matched = (previous == e);
    if (!matched) *expected = previous;
    return matched;
}
static INLINE uint64_t exchange_64_relaxed(atomic64_t* object, uint64_t desired) {
    uint64_t previous;
    asm volatile("xchgq %0, %1" : "=r"(previous), "+m"(object->_nonatomic) : "0"(desired));
    return previous;
}
static INLINE uint64_t fetch_add_64_relaxed(atomic64_t* object, int64_t operand) {
    uint64_t previous;
    asm volatile("lock; xaddq %0, %1" : "=r"(previous), "+m"(object->_nonatomic) : "0"(operand));
    return previous;
}
static INLINE uint64_t fetch_and_64_relaxed(atomic64_t* object, uint64_t operand) {
    uint64_t original;
    register uint64_t temp;
    asm volatile("1:     movq    %1, %0\n"
        "       movq    %0, %2\n"
        "       andq    %3, %2\n"
        "       lock; cmpxchgq %2, %1\n"
        "       jne     1b"
        : "=&a"(original), "+m"(object->_nonatomic), "=&r"(temp)
        : "r"(operand));
    return original;
}
static INLINE uint64_t fetch_or_64_relaxed(atomic64_t* object, uint64_t operand) {
    uint64_t original;
    register uint64_t temp;
    asm volatile("1:     movq    %1, %0\n"
        "       movq    %0, %2\n"
        "       orq     %3, %2\n"
        "       lock; cmpxchgq %2, %1\n"
        "       jne     1b"
        : "=&a"(original), "+m"(object->_nonatomic), "=&r"(temp)
        : "r"(operand));
    return original;
}
#elif CPU_X86
static INLINE uint64_t load_64_relaxed(const atomic64_t* object) {
    uint64_t original;
    asm volatile("movl %%ebx, %%eax\n"
                 "movl %%ecx, %%edx\n"
                 "lock; cmpxchg8b %1"
                 : "=&A"(original)
                 : "m"(object->_nonatomic));
    return original;
}
static INLINE void store_64_relaxed(atomic64_t* object, uint64_t desired) {
    uint64_t expected = object->_nonatomic;
    asm volatile("1:    cmpxchg8b %0\n"
                 "      jne 1b"
                 : "=m"(object->_nonatomic)
                 : "b"((uint32_t)desired), "c"((uint32_t)(desired >> 32)), "A"(expected));
}
static INLINE uint64_t compare_exchange_64_relaxed(atomic64_t* object, uint64_t expected, uint64_t desired) {
    uint64_t original;
    asm volatile("lock; cmpxchg8b %1"
                 : "=A"(original), "+m"(object->_nonatomic)
                 : "b"((uint32_t)desired), "c"((uint32_t)(desired >> 32)), "0"(expected));
    return original;
}
static INLINE intptr_t compare_exchange_weak_64_relaxed(atomic64_t* object, uint64_t* expected, uint64_t desired) {
    uint64_t e = *expected;
    uint64_t previous;
    intreg_t matched;
    asm volatile("lock; cmpxchg8b %1"
        : "=A"(previous), "+m"(object->_nonatomic)
        : "b"((uint32_t)desired), "c"((uint32_t)(desired >> 32)), "0"(e));
    matched = (previous == e);
    if (!matched) *expected = previous;
    return matched;
}
static INLINE uint64_t exchange_64_relaxed(atomic64_t* object, uint64_t desired) {
    uint64_t original = object->_nonatomic;
    for (;;) {
        uint64_t previous = compare_exchange_64_relaxed(object, original, desired);
        if (original == previous) return original;
        original = previous;
    }
}
static INLINE uint64_t fetch_add_64_relaxed(atomic64_t* object, int64_t operand) {
    for (;;) {
        uint64_t original = object->_nonatomic;
        if (compare_exchange_64_relaxed(object, original, original + operand) == original) return original;
    }
}
static INLINE uint64_t fetch_and_64_relaxed(atomic64_t* object, uint64_t operand) {
    for (;;) {
        uint64_t original = object->_nonatomic;
        if (compare_exchange_64_relaxed(object, original, original & operand) == original) return original;
    }
}
static INLINE uint64_t fetch_or_64_relaxed(atomic64_t* object, uint64_t operand) {
    for (;;) {
        uint64_t original = object->_nonatomic;
        if (compare_exchange_64_relaxed(object, original, original | operand) == original) return original;
    }
}
#elif COMPILER_GCC && CPU_ARM

/* Atomic types */
typedef struct { volatile uint32_t _nonatomic; } __attribute__((aligned(4))) atomic32_t;
typedef struct { volatile uint64_t _nonatomic; } __attribute__((aligned(8))) atomic64_t;
typedef struct { void* volatile _nonatomic; } __attribute__((aligned(PTR_SIZE))) atomic_t;

/* Atomic operations: */
#define signal_fence_consume() (0)
#define signal_fence_acquire() asm volatile("" ::: "memory")
#define signal_fence_release() asm volatile("" ::: "memory")
#define signal_fence_seq_cst() asm volatile("" ::: "memory")
#define thread_fence_consume() (0)
#if CPU_ARM_VERSION == 7
#define thread_fence_acquire() asm volatile("dmb ish" ::: "memory")
#define thread_fence_release() asm volatile("dmb ish" ::: "memory")
#define thread_fence_seq_cst() asm volatile("dmb ish" ::: "memory")
#elif CPU_ARM_VERSION == 6
#if CPU_ARM_THUMB
void thread_fence_acquire();
void thread_fence_release();
void thread_fence_seq_cst();
#else
#define thread_fence_acquire() asm volatile("mcr p15, 0, %0, c7, c10, 5" :: "r"(0) : "memory")
#define thread_fence_release() asm volatile("mcr p15, 0, %0, c7, c10, 5" :: "r"(0) : "memory")
#define thread_fence_seq_cst() asm volatile("mcr p15, 0, %0, c7, c10, 5" :: "r"(0) : "memory")
#endif
#endif
static INLINE uint32_t load_32_relaxed(const atomic32_t* object) { return object->_nonatomic; }
static INLINE void store_32_relaxed(atomic32_t* object, uint32_t desired) { object->_nonatomic = desired; }
#if (CPU_ARM_VERSION == 6) && CPU_ARM_THUMB
uint32_t compare_exchange_32_relaxed(atomic32_t* object, uint32_t expected, uint32_t desired);
uint32_t fetch_add_32_relaxed(atomic32_t* object, int32_t operand);
uint32_t fetch_and_32_relaxed(atomic32_t* object, uint32_t operand);
uint32_t fetch_or_32_relaxed(atomic32_t* object, uint32_t operand);
#else
static INLINE uint32_t compare_exchange_32_relaxed(atomic32_t* object, uint32_t expected, uint32_t desired) {
    uint32_t status;
    uint32_t original;
    asm volatile("1:     ldrex   %0, [%3]\n"
        "       cmp     %0, %4\n"
        "       bne     2f\n"
        "       strex   %1, %5, [%3]\n"
        "       cmp     %1, #0\n"
        "       bne     1b\n"
        "2:     ;"
        : "=&r"(original), "=&r"(status), "+Qo"(object->_nonatomic)
        : "r"(object), "Ir"(expected), "r"(desired)
        : "cc");
    return original;
}
static INLINE uint32_t fetch_add_32_relaxed(atomic32_t* object, int32_t operand) {
    uint32_t status;
    uint32_t original, desired;
    asm volatile("1:     ldrex   %0, [%4]\n"
        "       mov     %3, %0\n"
        "       add     %3, %5\n"
        "       strex   %1, %3, [%4]\n"
        "       cmp     %1, #0\n"
        "       bne     1b"
        : "=&r"(original), "=&r"(status), "+Qo"(object->_nonatomic), "=&r"(desired)
        : "r"(object), "Ir"(operand)
        : "cc");
    return original;
}
static INLINE uint32_t fetch_and_32_relaxed(atomic32_t* object, uint32_t operand) {
    uint32_t status;
    uint32_t original, desired;
    asm volatile("1:     ldrex   %0, [%4]\n"
        "       mov     %3, %0\n"
        "       and     %3, %5\n"
        "       strex   %1, %3, [%4]\n"
        "       cmp     %1, #0\n"
        "       bne     1b"
        : "=&r"(original), "=&r"(status), "+Qo"(object->_nonatomic), "=&r"(desired)
        : "r"(object), "Ir"(operand)
        : "cc");
    return original;
}
static INLINE uint32_t fetch_or_32_relaxed(atomic32_t* object, uint32_t operand) {
    uint32_t status;
    uint32_t original, desired;
    asm volatile("1:     ldrex   %0, [%4]\n"
        "       mov     %3, %0\n"
        "       orr     %3, %5\n"
        "       strex   %1, %3, [%4]\n"
        "       cmp     %1, #0\n"
        "       bne     1b"
        : "=&r"(original), "=&r"(status), "+Qo"(object->_nonatomic), "=&r"(desired)
        : "r"(object), "Ir"(operand)
        : "cc");
    return original;
}
#endif
uint64_t load_64_relaxed(const atomic64_t* object);
void store_64_relaxed(atomic64_t* object, uint64_t desired);
uint64_t compare_exchange_64_relaxed(atomic64_t* object, uint64_t expected, uint64_t desired);
uint64_t fetch_add_64_relaxed(atomic64_t* object, int64_t operand);
uint64_t fetch_and_64_relaxed(atomic64_t* object, uint64_t operand);
uint64_t fetch_or_64_relaxed(atomic64_t* object, uint64_t operand);
#endif
#endif


#if PTR_SIZE == 4
INLINE size_t atomic_load(const atomic_t* object, MemoryOrder order) {
    size_t result = load_32_relaxed(object);
    if (order == MEMORY_ACQUIRE || order == MEMORY_ACQ_REL) 
        thread_fence_acquire();
    return result;
}

INLINE void atomic_store(atomic_t* object, size_t desired, MemoryOrder order) {
    if (order == MEMORY_RELEASE || order == MEMORY_ACQ_REL)
        thread_fence_release();
    store_32_relaxed(object, desired);
}

INLINE size_t atomic_exchange(atomic_t* object, size_t desired, MemoryOrder order) {
    if (order == MEMORY_RELEASE || order == MEMORY_ACQ_REL)
        thread_fence_release();
    size_t result = exchange_32_relaxed(object, desired);
    if (order == MEMORY_ACQUIRE || order == MEMORY_ACQ_REL)
        thread_fence_acquire();
    return result;
}

INLINE size_t atomic_fetch_add(atomic_t* object, ptrdiff_t operand, MemoryOrder order) {
    if (order == MEMORY_RELEASE || order == MEMORY_ACQ_REL)
        thread_fence_release();
    size_t result = fetch_add_32_relaxed(object, operand);
    if (order == MEMORY_ACQUIRE || order == MEMORY_ACQ_REL)
        thread_fence_acquire();
    return result;
}

INLINE size_t atomic_fetch_and(atomic_t* object, size_t operand, MemoryOrder order) {
    if (order == MEMORY_RELEASE || order == MEMORY_ACQ_REL)
        thread_fence_release();
    size_t result = fetch_and_32_relaxed(object, operand);
    if (order == MEMORY_ACQUIRE || order == MEMORY_ACQ_REL)
        thread_fence_acquire();
    return result;
}

INLINE size_t atomic_fetch_or(atomic_t* object, size_t operand, MemoryOrder order) {
    if (order == MEMORY_RELEASE || order == MEMORY_ACQ_REL)
        thread_fence_release();
    size_t result = fetch_or_32_relaxed(object, operand);
    if (order == MEMORY_ACQUIRE || order == MEMORY_ACQ_REL)
        thread_fence_acquire();
    return result;
}

INLINE size_t atomic_compare_exchange(atomic_t* object, size_t expected, size_t desired, MemoryOrder order) {
    if (order == MEMORY_RELEASE || order == MEMORY_ACQ_REL)
        thread_fence_release();
    size_t result = compare_exchange_32_relaxed(object, expected, desired);
    if (order == MEMORY_ACQUIRE || order == MEMORY_ACQ_REL)
        thread_fence_acquire();
    return result;
}

INLINE bool atomic_compare_exchange_weak(atomic_t* object, size_t* expected, size_t desired, int success, int failure) {
    if ((success == MEMORY_RELEASE || success == MEMORY_ACQ_REL) || (failure == MEMORY_RELEASE || failure == MEMORY_ACQ_REL))
        thread_fence_release();
    intptr_t result = compare_exchange_weak_32_relaxed(object, expected, desired);
    if (result && (success == MEMORY_ACQUIRE || success == MEMORY_ACQ_REL))
        thread_fence_acquire();
    else if (failure == MEMORY_ACQUIRE || failure == MEMORY_ACQ_REL)
        thread_fence_acquire();
    return result;
}

INLINE bool atomic_compare_exchange_strong(atomic_t* object, size_t expected, size_t desired, MemoryOrder order) {
    size_t prev = atomic_compare_exchange(object, expected, desired, order);
    bool result = (prev == expected);
    if (!result)
        expected = prev;
    return result;
}

#elif PTR_SIZE == 8

INLINE size_t atomic_load(const atomic_t* object, MemoryOrder order) {
    size_t result = load_64_relaxed(object);
    if (order == MEMORY_ACQUIRE || order == MEMORY_ACQ_REL)
        thread_fence_acquire();
    return result;
}

INLINE void atomic_store(atomic_t* object, size_t desired, MemoryOrder order) {
    if (order == MEMORY_RELEASE || order == MEMORY_ACQ_REL)
        thread_fence_release();
    store_64_relaxed(object, desired);
}

INLINE size_t atomic_exchange(atomic_t* object, size_t desired, MemoryOrder order) {
    if (order == MEMORY_RELEASE || order == MEMORY_ACQ_REL)
        thread_fence_release();
    size_t result = exchange_64_relaxed(object, desired);
    if (order == MEMORY_ACQUIRE || order == MEMORY_ACQ_REL)
        thread_fence_acquire();
    return result;
}

INLINE size_t atomic_fetch_add(atomic_t* object, ptrdiff_t operand, MemoryOrder order) {
    if (order == MEMORY_RELEASE || order == MEMORY_ACQ_REL)
        thread_fence_release();
    size_t result = fetch_add_64_relaxed(object, operand);
    if (order == MEMORY_ACQUIRE || order == MEMORY_ACQ_REL)
        thread_fence_acquire();
    return result;
}

INLINE size_t atomic_fetch_and(atomic_t* object, size_t operand, MemoryOrder order) {
    if (order == MEMORY_RELEASE || order == MEMORY_ACQ_REL)
        thread_fence_release();
    size_t result = fetch_and_64_relaxed(object, operand);
    if (order == MEMORY_ACQUIRE || order == MEMORY_ACQ_REL)
        thread_fence_acquire();
    return result;
}

INLINE size_t atomic_fetch_or(atomic_t* object, size_t operand, MemoryOrder order) {
    if (order == MEMORY_RELEASE || order == MEMORY_ACQ_REL)
        thread_fence_release();
    size_t result = fetch_or_64_relaxed(object, operand);
    if (order == MEMORY_ACQUIRE || order == MEMORY_ACQ_REL)
        thread_fence_acquire();
    return result;
}

INLINE size_t atomic_compare_exchange(atomic_t* object, size_t expected, size_t desired, MemoryOrder order) {
    if (order == MEMORY_RELEASE || order == MEMORY_ACQ_REL)
        thread_fence_release();
    size_t result = compare_exchange_64_relaxed(object, expected, desired);
    if (order == MEMORY_ACQUIRE || order == MEMORY_ACQ_REL)
        thread_fence_acquire();
    return result;
}

INLINE bool atomic_compare_exchange_weak(atomic_t* object, size_t* expected, size_t desired, int success, int failure) {
    if ((success == MEMORY_RELEASE || success == MEMORY_ACQ_REL) || (failure == MEMORY_RELEASE || failure == MEMORY_ACQ_REL))
        thread_fence_release();
    intptr_t result = compare_exchange_weak_64_relaxed(object, expected, desired);
    if (result && (success == MEMORY_ACQUIRE || success == MEMORY_ACQ_REL))
        thread_fence_acquire();
    else if (failure == MEMORY_ACQUIRE || failure == MEMORY_ACQ_REL)
        thread_fence_acquire();
    return result;
}

INLINE bool atomic_compare_exchange_strong(atomic_t* object, size_t expected, size_t desired, MemoryOrder order) {
    size_t prev = atomic_compare_exchange(object, expected, desired, order);
    bool result = (prev == expected);
    if (!result)
        expected = prev;
    return result;
}

#else
#error PTR_SIZE not set!
#endif