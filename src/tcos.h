/*==========================================================*/
/*							PLATFORM/OS						*/
/*==========================================================*/
#pragma once
#include "tccore.h"

#include <stddef.h>


/* Platfrom specific: */

inline size_t page_size();
inline size_t cache_size();


#define WORD_BITS (8 * sizeof(unsigned int))
#define MAX_PATH_LENGTH 256


#if COMPILER_MSVC
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN

#include <malloc.h>
#include <stdlib.h>

#define INLINE __forceinline
#define NOINLINE __declspec(noinline)
#define THREAD_LOCAL __declspec(thread)
#define ALIGNED(decl, amt) __declspec(align(amt)) decl

#define aligned_alloc(a, s) _aligned_malloc(s, a)
#define aligned_realloc(a, s, p) _aligned_realloc(p, s, a)
#define aligned_free(s) _aligned_free(s)

inline size_t page_size() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
}

inline size_t cache_size() {
    size_t line_size = 0;
    DWORD buffer_size = 0;
    DWORD i = 0;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION* buffer = 0;
    GetLogicalProcessorInformation(0, &buffer_size);
    buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)malloc(buffer_size);
    GetLogicalProcessorInformation(&buffer[0], &buffer_size);
    for (i = 0; i != buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); ++i) {
        if (buffer[i].Relationship == RelationCache && buffer[i].Cache.Level == 1) {
            line_size = buffer[i].Cache.LineSize;
            break;
        }
    }
    free(buffer);
    return line_size;
}

#include <winbase.h>
#define pause() _mm_pause()

inline void set_thread_affinity(HANDLE thread, int cpu_num) {
    DWORD_PTR prev_mask = SetThreadAffinityMask(thread, 1 << (cpu_num));
}

inline HANDLE get_current_thread() {
    HANDLE h = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS))
        abort();
    return h;
}

#elif COMPILER_GCC
#include <stddef.h>
#include <unistd.h>

#define INLINE __attribute__((always_inline))
#define NOINLINE __attribute__ ((noinline))
#define THREAD_LOCAL __thread
#define ALIGNED(decl, amt) decl __attribute__((aligned(amt)))

inline size_t page_size() { return getpagesize(); }

#define aligned_free(s) free(s)

#if (CPU_X86 || CPU_X64)
#define pause() __builtin_ia32_pause()
#endif

inline void set_thread_affinity(pthread_t thread, int cpu_num) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_num, &cpuset);
    int err = pthread_setaffinity_np(thread, sizeof(cpu_set_t), cpuset);
}

inline pthread_t get_current_thread() { return pthread_self(); }

#if IS_APPLE
#include <sys/sysctl.h>
inline size_t cache_size() {
    size_t line_size = 0;
    size_t sizeof_line_size = sizeof(line_size);
    sysctlbyname("hw.cachelinesize", &line_size, &sizeof_line_size, 0, 0);
    return line_size;
}
#else
#include <stdio.h>
inline size_t cache_size() { return sysconf(_SC_LEVEL1_DCACHE_LINESIZE); }
#endif

#else
#error Unsupported platform!
#endif