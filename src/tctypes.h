/*==========================================================*/
/*							TYPES							*/
/*==========================================================*/
#pragma once
#include "tccore.h"

#if HAS_STDINT
#include <stdint.h>
#else
typedef signed char int8_t;
typedef unsigned char uint8_t;
#define INT8_MIN (-0x7f - 1)
#define INT8_MAX 0x7f
#define UINT8_MAX 0xff

typedef short int16_t;
typedef unsigned short uint16_t;
#define INT16_MIN (-0x7fff - 1)
#define INT16_MAX 0x7fff
#define UINT16_MAX 0xffff

typedef int int32_t;
typedef unsigned int uint32_t;
#define INT32_MIN (-0x7fffffff - 1)
#define INT32_MAX 0x7fffffff
#define UINT32_MAX 0xffffffff

typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#define INT64_MIN (-0x7fffffffffffffff - 1)
#define INT64_MAX 0x7fffffffffffffff
#define UINT64_MAX 0xffffffffffffffffu
#endif

#if HAS_STDBOOL
#include <stdbool.h>
#else
typedef int bool;
#define true 1
#define false 0
#endif

typedef struct {
	union {
		uint64_t data;
		double nr;
		void* ptr;
	};
} Variant;

typedef unsigned char byte;
typedef unsigned int uint;

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

typedef uint32_t Color;
typedef uint32_t StringID;
typedef uint64_t rid_t;

typedef struct listnode_s listnode_t;

typedef struct slab_arena_s slab_arena_t;
typedef struct slab_cache_s slab_cache_t;

typedef struct text_t {
	int len;							// Text length
	char* data;							// Location of text
} text_t;

typedef struct image_t {
	int width, height, format;			// Number of pixel and pixel format
	uint8_t* data;						// Location of pixels on CPU
} image_t;