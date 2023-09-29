/*==========================================================*/
/*							CORE							*/
/*==========================================================*/
#pragma once
#include <assert.h>
#include <stdio.h>

#ifndef TC_ENGINE_NAME
#define TC_ENGINE_NAME "TCEngine"
#endif

#ifndef TC_DEBUG and defined(DEBUG)
#define TC_DEBUG 1
#endif

#ifndef TC_ENABLE_ASSERT and defines(DEBUG)
#define TC_ENABLE_ASSERT 1
#endif

#if defined(_WIN32)
	#define COMPILER_MSVC 1
	#if _MSC_VER >= 1600
	#define HAS_STDINT 1
	#endif
	#if _MSC_VER >= 1800
	#define HAS_STDBOOL 1
	#endif
	#if defined(_M_X64)
	#define CPU_X64 1
	#define PTR_SIZE 8
	#elif defined(_M_IX86)
	#define CPU_X86 1
	#define PTR_SIZE 4
	#else
	#error Unrecognized platform!
	#endif
#elif defined(__GNUC__)
#define COMPILER_GCC 1
#define HAS_STDINT 1
#if defined(__llvm__)
#define COMPILER_LLVM 1
#if __has_feature(c_atomic)
#define HAS_C11_MEMORY_MODEL 1
#endif
#endif
#if defined(__APPLE__)
#define IS_APPLE 1
#endif
#if defined(__x86_64__)
#define CPU_X64 1
#define PTR_SIZE 8
#elif defined(__i386__)
#define CPU_X86 1
#define PTR_SIZE 4
#elif defined(__arm__)
#define CPU_ARM 1
#if defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__)
#define CPU_ARM_VERSION 7
#define PTR_SIZE 4
#elif defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6T2__) || defined(__ARM_ARCH_6Z__) || defined(__ARM_ARCH_6ZK__)
#define CPU_ARM_VERSION 6
#define PTR_SIZE 4
#else
#error Unrecognized ARM CPU architecture version!
#endif
#if defined(__thumb__)
#define CPU_ARM_THUMB 1
#endif
#else
#error Unrecognized target CPU!
#endif
#else
#error Unrecognized compiler!
#endif

/* Utility macros: */
#define SGN(_v) (((_v) < 0) ? (-1.0) : (+1.0))

#define CONCAT_IMPL( x, y ) x##y
#define MACRO_CONCAT( x, y ) CONCAT_IMPL( x, y )
#define UNIQUE_INDEX(_i) MACRO_CONCAT(i, __COUNTER__)
#define INDEX(_i) MACRO_CONCAT(_i,__LINE__)

#define TC_COUNT(_x) (sizeof(_x) / sizeof(*(_x)))

#define TC_ERROR(...) TRACE(LOG_ERROR, ##__VA_ARGS__)

#ifdef TC_DEBUG
#if defined(COMPILER_MSVC)
#define TC_BREAK() __debugbreak()
#else
#include <signal.h>
#define TC_BREAK() raise(SIGTRAP)
#endif
#else
#define TC_BREAK()
#endif

#ifdef TC_ENABLE_ASSERT
#define TC_ASSERT(_x, ...) { if(!(_x)) {TC_ERROR("Assertion Failed: {0}", ##__VA_ARGS__); TC_BREAK();}}
#else
#define TC_ASSERT(x, ...)
#endif

#define SWAP(x, y) do { \
    unsigned char swap_temp[sizeof(x) == sizeof(y) ? (signed)sizeof(x) : -1]; \
    memcpy(swap_temp,&y,sizeof(x)); \
    memcpy(&y,&x,       sizeof(x)); \
    memcpy(&x,swap_temp,sizeof(x)); \
} while(0)

#define container_of(ptr, type, member) (type *)( (char *)(ptr) - offsetof(type,member) )

/*==========================================================*/
/*							PLATFORM/OS						*/
/*==========================================================*/

#include <stddef.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* Platfrom specific: */

#define WORD_BITS (8 * sizeof(unsigned int))
#define MAX_PATH_LENGTH 256

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN

#define INLINE __forceinline
#define NOINLINE __declspec(noinline)
#define THREAD_LOCAL __declspec(thread)
#define ALIGNED(decl, amt) __declspec(align(amt)) decl


#include <winbase.h>
#define pause() _mm_pause()

#elif defined(_HAVE_POSIX)
#include <stddef.h>
#include <unistd.h>

#define INLINE __attribute__((always_inline))
#define NOINLINE __attribute__ ((noinline))
#define THREAD_LOCAL __thread
#define ALIGNED(decl, amt) decl __attribute__((aligned(amt)))


#if (CPU_X86 || CPU_X64)
#define pause() __builtin_ia32_pause()
#endif

#else
#error Unsupported platform!
#endif

/*==========================================================*/
/*							LOGGING							*/
/*==========================================================*/

/* Logging functions: */
enum logtype_t {
	LOG_ALL,
	LOG_INFO,
	LOG_DEBUG,
	LOG_WARNING,
	LOG_ERROR,
	LOG_NONE
};

typedef void (*debugcb_t)(enum logtype_t severity, const char* message, const char* function, const char* file, int line);

#define TRACE(level, M, ...) fprintf(stdout, "[" #level "] " M " (%s:%d)\n", ##__VA_ARGS__, __FILE__, __LINE__)


/*==========================================================*/
/*							TYPES							*/
/*==========================================================*/

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	union {
		uint64_t data;
		double nr;
		void* ptr;
	};
} variant_t;

// Define math types
//TODO: allign to simd
typedef float vec2[2];
typedef float vec3[3];
typedef float vec4[4];
typedef vec2 mat2[2];
typedef vec3 mat3[3];
typedef vec4 mat4[4];
typedef int ivec2[2];
typedef int ivec3[3];
typedef int ivec4[4];
typedef float rect2[4];
typedef int irect2[4];

typedef uint32_t color_t;

typedef uint64_t sid_t;

typedef struct { uint64_t x[2]; } tc_uuid_t;

typedef union {
	struct {
		uint64_t index : 32;
		uint64_t gen : 22;
		uint64_t type : 10;
	};
	uint64_t handle;
} tc_rid_t;

/*
 * Cross-platform file descriptor
 */
typedef struct { uint64_t handle; } fd_t;


/*==========================================================*/
/*							ATOMICS							*/
/*==========================================================*/

#include <stdatomic.h>

/* Atomic types */
typedef _Atomic(uint32_t) atomic32_t;
typedef _Atomic(uint64_t) atomic64_t;
typedef _Atomic(size_t) atomic_t;

#define CAS(_x, _y, _z) \
atomic_compare_exchange_strong_explicit((atomic_t*)(_x), (size_t*)&(_y), (size_t)(_z), memory_order_acq_rel, memory_order_acq_rel)


/*==========================================================*/
/*							LOCKS							*/
/*==========================================================*/

typedef struct lock_s { atomic_t value; } lock_t;

#define TC_LOCK(l) spin_lock(l)
#define TC_UNLOCK(l) spin_unlock(l)

//TODO: Make fiber waiting spin lock after x amount of cycles
static inline
void spin_lock(lock_t* lock) {
    for (;;) {
        if (!atomic_exchange_explicit(&lock->value, true, memory_order_acquire)) {
            break;
        }
        while (atomic_load_explicit(&lock->value, memory_order_relaxed)) {
            pause();
        }
    }
}

static inline
void spin_unlock(lock_t* lock) {
    atomic_store_explicit(&lock->value, false, memory_order_release);
}



/*==========================================================*/
/*							COLORS							*/
/*==========================================================*/

// Name,		RGBA 32bit
#define BASECOLORS \
X(BLANK,		0x00000000)\
X(BLACK,		0x000000FF)\
X(DARKBLUE,		0x0000AAFF)\
X(DARKGREEN,	0x00AA00FF)\
X(DARKAQUA,		0x00AAAAFF)\
X(DARKRED,		0xAA0000FF)\
X(DARKPURPLE,	0xAA00AAFF)\
X(GOLD,			0xFFAA00FF)\
X(GRAY,			0xAAAAAAFF)\
X(DARKGRAY,		0x555555FF)\
X(BLUE,			0x5555FFFF)\
X(GREEN,		0x55FF55FF)\
X(AQUA,			0x55FFFFFF)\
X(RED,			0xFF5555FF)\
X(LIGHTPURPLE,	0xFF55FFFF)\
X(YELLOW,		0xFFFF55FF)\
X(WHITE,		0xFFFFFFFF)

typedef enum {
#define X(name, rgba) name = rgba,
	BASECOLORS
#undef X
} BaseColor;


/*==========================================================*/
/*							INPUT							*/
/*==========================================================*/

// Name,	Codepoint
#define KEYBOARDKEYS \
X(NO,		0)\
X(APOSTR,	39)\
X(COMMA,	44)\
X(MINUS,	45)\
X(PERIOD,	46)\
X(SLASH,	47)\
X(ZERO,		48)\
X(ONE,		49)\
X(TWO,		50)\
X(THREE,	51)\
X(FOUR,		52)\
X(FIVE,		53)\
X(SIX,		54)\
X(SEVEN,	55)\
X(EIGHT,	56)\
X(NINE,		57)\
X(SEMICOLON,59)\
X(EQUAL,	61)\
X(A,		65)\
X(B,		66)\
X(C,		67)\
X(D,		68)\
X(E,		69)\
X(F,		70)\
X(G,		71)\
X(H,		72)\
X(I,		73)\
X(J,		74)\
X(K,		75)\
X(L,		76)\
X(M,		77)\
X(N,		78)\
X(O,		79)\
X(P,		80)\
X(Q,		81)\
X(R,		82)\
X(S,		83)\
X(T,		84)\
X(U,		85)\
X(V,		86)\
X(W,		87)\
X(X,		88)\
X(Y,		89)\
X(Z,		90)\
X(SPACE,	32)\
X(ENTER,	257)\
X(ESC,		256)\
X(RIGHT,	262)\
X(LEFT,		263)\
X(DOWN,		264)\
X(UP,		265)\
X(LSHIFT,	340)\
X(LCTRL,	341)\
X(LALT,		342)\
X(RSHIFT,	344)\
X(RCTRL,	345)\
X(RALT,		346)\
X(BACKSPACE,259)\
X(TAB,		258)\
X(HOME,		268)\
X(END,		269)\
X(DELETE,	261)\
X(INSERT,	260)\
X(PAGEUP,	266)\
X(PAGEDOWN, 267)\
X(PAUSE,	284)\
X(PRTSCREEN,283)\
X(F1,		290)\
X(F2,		291)\
X(F3,		292)\
X(F4,		293)\
X(F5,		294)\
X(F6,		295)\
X(F7,		296)\
X(F8,		297)\
X(F9,		298)\
X(F10,		299)\
X(F11,		300)\
X(F12,		301)

typedef enum {
#define X(_enum, _val) KEY_##_enum = _val,
	KEYBOARDKEYS
#undef X
} KeyboardKey;

typedef enum {
	MOUSE_LEFT,
	MOUSE_RIGHT,
	MOUSE_MIDDLE,
} MouseButton;


/*==========================================================*/
/*							WINDOW							*/
/*==========================================================*/

// Window creation flags:
enum window_flags {
	WHIDDEN = 1 << 0,
	WRESIZABLE = 1 << 1,
	WUNDECORATED = 1 << 2,
	WTRANSPARENT = 1 << 3,
	WVSYNC = 1 << 4,
	WCONTINUEMINIMIZED = 1 << 5,
};

enum graphics_library_t {
    GL_VULKAN,
    GL_OPENGL,
};

void window_create(int width, int height, const char* title);
int window_alive(void);
void window_close(void);
void* window_handle();
const char* window_get_title();
double time_get(void);
void inputs_poll(void);
void swap_buffers(void);
