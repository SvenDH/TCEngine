/*==========================================================*/
/*							MATH							*/
/*==========================================================*/
#pragma once
#include "core.h"
#include "log.h"
#include "types.h"

#include <string.h>
#include <math.h>
#include <float.h>

#define VEC2_ZERO (vec2){ 0.0f, 0.0f }
#define MAT3_ZERO { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } }
#define MAT3_IDENTITY { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
#define NULL_RECT (rect2){0.0f, 0.0f, 0.0f, 0.0f}
#define EPSILON 0.0001f

enum comparefunc_t {
	COMPARE_FAIL = 0,
	COMPARE_LESS,
	COMPARE_LEQUAL,
	COMPARE_EQUAL,
	COMPARE_GEQUAL,
	COMPARE_GREATER,
	COMPARE_NOTEQUAL,
	COMPARE_PASS,
};


/* Integer utils: */
static const int log2_tab32[32] = {
	 0,  9,  1, 10, 13, 21,  2, 29,
	11, 14, 16, 18, 22, 25,  3, 30,
	 8, 12, 20, 28, 15, 17, 24,  7,
	19, 27, 23,  6, 26,  5,  4, 31
};

// Returns log2
inline int log2_32(uint32_t value) {
	value |= value >> 1;
	value |= value >> 2;
	value |= value >> 4;
	value |= value >> 8;
	value |= value >> 16;
	return log2_tab32[(uint32_t)(value * 0x07C4ACDD) >> 27];
}

inline bool is_power_of_2(uint32_t val) {
	return val > 0 && (val & (val - 1)) == 0;
}

inline size_t align_down(size_t val, uint32_t align) {
	TC_ASSERT(is_power_of_2(align), "[Math]: Alignment (%i) must be power of 2", align);
	return val & -align;
}

inline size_t align_up(size_t val, uint32_t align) {
	TC_ASSERT(is_power_of_2(align), "[Math]: Alignment (%i) must be power of 2", align);
	return (val + (align - 1)) & -align;
}

// Returns the next power of 2 from x
inline uint32_t next_power_of_2(uint32_t x) {
	if (x==0)
		return 0;
	
	--x;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;

	return ++x;
}

// Returns highest power of 2 under x
inline uint32_t previous_power_of_2(uint32_t x) {
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	return x - (x >> 1);
}

// Returns the closest power of 2 to x
inline unsigned int closest_power_of_2(unsigned int x) {
	unsigned int nx = next_power_of_2(x);
	unsigned int px = previous_power_of_2(x);
	return (nx - x) > (x - px) ? px : nx;
}

inline int get_shift_from_power_of_2(unsigned int p_bits) {
	for (unsigned int i = 0; i < 32; i++) {
		if (p_bits == (unsigned int)(1 << i)) {
			return i;
		}
	}
	return -1;
}

inline int int_floor(double x) {
	int i = (int)x;
	return i - (i > x);
}

/* Float utils: */

inline float residual(float p) {
	return p - int_floor(p);
}

inline float real_abs(float n) {
	return (n > 0) ? n : -n;
}

/* Vector utils: */

inline float vec2_len(vec2 v) {
	return sqrt((double)(v[0] * v[0] + v[1] * v[1]));
}

inline void vec2_normalize(vec2 v) {
	float len = vec2_len(v);
	if (len > EPSILON) {
		float invLen = 1.0f / len;
		v[0] *= invLen;
		v[1] *= invLen;
	}
}

inline float vec2_dot(const vec2 a, const vec2 b) {
	return a[0] * b[0] + a[1] * b[1];
}

inline float vec2_cross(const vec2 a, const vec2 b) {
	return a[0] * b[1] + a[1] * b[0];
}

/* Matrix utils: */

inline void mat_identity(mat3 t) {
	memset(t, 0, sizeof(mat3));
	t[0][0] = t[1][1] = t[2][2] = 1.0f;
}

inline void mat_ortho(mat3 t, float left, float right, float bottom, float top) {
	memset(t, 0, sizeof(mat3));
	t[0][0] = 2.f / (right - left);
	t[1][1] = 2.f / (top - bottom);

	t[2][0] = -(right + left) / (right - left);
	t[2][1] = -(top + bottom) / (top - bottom);

	t[2][2] = -1.f;
}

inline void mat_mul(mat3 d, mat3 t, mat3 m) {
	d[0][0] = (t[0][0] * m[0][0]) + (t[1][0] * m[0][1]) + (t[2][0] * m[0][2]);
	d[1][0] = (t[0][0] * m[1][0]) + (t[1][0] * m[1][1]) + (t[2][0] * m[1][2]);
	d[2][0] = (t[0][0] * m[2][0]) + (t[1][0] * m[2][1]) + (t[2][0] * m[2][2]);

	d[0][1] = (t[0][1] * m[0][0]) + (t[1][1] * m[0][1]) + (t[2][1] * m[0][2]);
	d[1][1] = (t[0][1] * m[1][0]) + (t[1][1] * m[1][1]) + (t[2][1] * m[1][2]);
	d[2][1] = (t[0][1] * m[2][0]) + (t[1][1] * m[2][1]) + (t[2][1] * m[2][2]);

	d[0][2] = (t[0][2] * m[0][0]) + (t[1][2] * m[0][1]) + (t[2][2] * m[0][2]);
	d[1][2] = (t[0][2] * m[1][0]) + (t[1][2] * m[1][1]) + (t[2][2] * m[1][2]);
	d[2][2] = (t[0][2] * t[2][0]) + (t[1][2] * m[2][1]) + (t[2][2] * m[2][2]);
}

inline void mat_transform(mat3 t, float x, float y, float angle, float sx, float sy, float ox, float oy, float kx, float ky) {
	float c = cosf(angle), s = sinf(angle);
	// |1    x| |c -s  | |sx     | | 1 ky  | |1   -ox|
	// |  1  y| |s  c  | |   sy  | |kx  1  | |  1 -oy|
	// |     1| |     1| |      1| |      1| |     1 |
	//   move    rotate    scale     skew      origin
	t[0][0] = c * sx - ky * s * sy; // = a
	t[0][1] = s * sx + ky * c * sy; // = b
	t[1][0] = kx * c * sx - s * sy; // = c
	t[1][1] = kx * s * sx + c * sy; // = d
	t[2][0] = x - ox * t[0][0] - oy * t[1][0];
	t[2][1] = y - ox * t[0][1] - oy * t[1][1];

	t[0][2] = t[1][2] = 0.0f;
	t[2][2] = 1.0f;
}

/* Rectangle utils: */

inline void rect_zero(rect2 a) {
	a[0] = 0.0f; a[1] = 0.0f; a[2] = 0.0f; a[3] = 0.0f;
}

inline void rect_div(rect2 a, float d) {
	a[0] /= d;
	a[1] /= d;
	a[2] /= d;
	a[3] /= d;
}

inline void rect_floor(rect2 a) {
	//TODO: use SIMD _mm_setcsr _mm_cvtps_epi32
	a[0] = (float)int_floor(a[0]);
	a[1] = (float)int_floor(a[1]);
	a[2] = (float)int_floor(a[2]);
	a[3] = (float)int_floor(a[3]);
}

inline void rect_cpy(rect2 d, rect2 s) {
	d[0] = s[0];
	d[1] = s[1];
	d[2] = s[2];
	d[3] = s[3];
}

inline int rect_overlap(rect2 a, rect2 b) {
	return !((int)a[0] > (int)b[2]
		|| (int)a[1] > (int)b[3]
		|| (int)a[2] < (int)b[0]
		|| (int)a[3] < (int)b[1]);
}

inline int rect_segment(rect2 a, vec2 from, vec2 to, vec2 normal, vec2 pos) {
	float min = 0, max = 1;
	int ax = 0;
	float sign = 0;
	for (int i = 0; i < 2; i++) {
		float seg_from = from[i];
		float seg_to = to[i];
		float box_begin = a[i];
		float box_end = a[2 + i];
		float cmin, cmax, csign;
		if (seg_from < seg_to) {
			if (seg_from > box_end || seg_to < box_begin)
				return 0;
			float length = seg_to - seg_from;
			cmin = (seg_from < box_begin) ? ((box_begin - seg_from) / length) : 0;
			cmax = (seg_to > box_end) ? ((box_end - seg_from) / length) : 1;
			csign = -1.0;
		}
		else {
			if (seg_to > box_end || seg_from < box_begin)
				return 0;
			float length = seg_to - seg_from;
			cmin = (seg_from > box_end) ? (box_end - seg_from) / length : 0;
			cmax = (seg_to < box_begin) ? (box_begin - seg_from) / length : 1;
			csign = 1.0;
		}
		if (cmin > min) {
			min = cmin;
			ax = i;
			sign = csign;
		}
		if (cmax < max)
			max = cmax;
		if (max < min)
			return 0;
	}
	vec2 rel = { to[0] - from[0], to[1] - from[1] };
	if (normal) {
		normal[ax] = sign;
		normal[1-ax] = 0;
	}
	if (pos) {
		pos[0] = from[0] + rel[0] * min;
		pos[1] = from[1] + rel[1] * min;
	}
	return 1;
}

inline void rect_correct(rect2 a) {
	SWAP(a[0], a[2]);
	SWAP(a[1], a[3]);
}

inline void irect_cpy(irect2 d, irect2 s) {
	d[0] = s[0];
	d[1] = s[1];
	d[2] = s[2];
	d[3] = s[3];
}

inline void irect_totiles(irect2 d, rect2 s, int size) {
	d[0] = int_floor(s[0] / size);
	d[1] = int_floor(s[1] / size);
	d[2] = int_floor(s[2] / size);
	d[3] = int_floor(s[3] / size);
}

/* Color utils: */

inline void color_to_vec(vec4 d, color_t c) {
	d[0] = (float)((c >> 24) & 0xFF) / 255;
	d[1] = (float)((c >> 16) & 0xFF) / 255;
	d[2] = (float)((c >> 8) & 0xFF) / 255;
	d[3] = (float)(c & 0xFF) / 255;
}


/* Hashing utils: */

inline uint32_t crc32_b(const unsigned char* str, uint32_t len) {
	static const unsigned long crc32_tab[] = {
		0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
		0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
		0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
		0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
		0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
		0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
		0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
		0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
		0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
		0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
		0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
		0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
		0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
		0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
		0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
		0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
		0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
		0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
		0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
		0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
		0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
		0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
		0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
		0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
		0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
		0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
		0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
		0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
		0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
		0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
		0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
		0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
		0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
		0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
		0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
		0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
		0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
		0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
		0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
		0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
		0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
		0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
		0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
		0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
		0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
		0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
		0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
		0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
		0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
		0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
		0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
		0x2d02ef8dL
	};
	uint32_t crc32val = 0;
	for (uint32_t i = 0; i < len; i++) {
		crc32val = crc32_tab[(crc32val ^ str[i]) & 0xff] ^ (crc32val >> 8);
	}
	return crc32val;
}
