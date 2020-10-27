/*==========================================================*/
/*							CORE							*/
/*==========================================================*/
#pragma once
#include <assert.h>
#include <stdio.h>

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
#define EXPAND(x) x

#define _NARGS_IMPL(x1,x2,x3,x4,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15,N,...) N
#define VA_NARGS(...) EXPAND(_NARGS_IMPL(__VA_ARGS__,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0))

#define SGN(_v) (((_v) < 0) ? (-1.0) : (+1.0))

#define CONCAT_IMPL( x, y ) x##y
#define MACRO_CONCAT( x, y ) CONCAT_IMPL( x, y )
#define UNIQUE_INDEX(_i) MACRO_CONCAT(i, __COUNTER__)
#define INDEX(_i) MACRO_CONCAT(_i,__LINE__)


#define TC_ERROR(...) TRACE(LOG_ERROR, EXPAND(__VA_ARGS__))

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
#define TC_ASSERT(_x, ...) { if(!(_x)) {TC_ERROR("Assertion Failed: {0}", __VA_ARGS__); TC_BREAK();}}
#else
#define TC_ASSERT(x, ...)
#endif

#define SWAP(x, y) do \
{ unsigned char swap_temp[sizeof(x) == sizeof(y) ? (signed)sizeof(x) : -1]; \
    memcpy(swap_temp,&y,sizeof(x)); \
    memcpy(&y,&x,       sizeof(x)); \
    memcpy(&x,swap_temp,sizeof(x)); \
} while(0)

#define container_of(ptr, type, member) (type *)( (char *)(ptr) - offsetof(type,member) )

#define TC_ENUM(_name, _type) typedef _type _name; enum

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
typedef enum {
	WHIDDEN = 1 << 0,
	WRESIZABLE = 1 << 1,
	WUNDECORATED = 1 << 2,
	WTRANSPARENT = 1 << 3,
	WVSYNC = 1 << 4,
	WCONTINUEMINIMIZED = 1 << 5,
} WindowFlags;

// Window state:
typedef enum {
	WREADY = 1 << 0,
	WMINIMIZED = 1 << 1,
	WFOCUSED = 1 << 2,
	WRESIZED = 1 << 3,
	WFULLSCREEN = 1 << 4,
	WCLOSED = 1 << 5,
} WindowState;

void window_create(int width, int height, const char* title);
int window_alive(void);
void window_close(void);
double time_get(void);
void inputs_poll(void);
void swap_buffers(void);

