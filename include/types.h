/*==========================================================*/
/*							TYPES							*/
/*==========================================================*/
#pragma once
#include "core.h"

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