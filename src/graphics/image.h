/*==========================================================*/
/*							IMAGES							*/
/*==========================================================*/
#pragma once
#include "types.h"

//#define STBI_NO_STDIO
//#define STB_IMAGE_IMPLEMENTATION
//#include <stb_image.h>


/* Pixel Format */
#define PIXELFORMAT \
X(BC1)\
X(BC2)\
X(BC3)\
X(BC4)\
X(BC5)\
X(BC6H)\
X(BC7)\
X(Unknown)\
X(R1)\
X(A8)\
X(R8)\
X(R8I)\
X(R8U)\
X(R8S)\
X(R16)\
X(R16I)\
X(R16U)\
X(R16F)\
X(R16S)\
X(R32I)\
X(R32U)\
X(R32F)\
X(RG8)\
X(RG8I)\
X(RG8U)\
X(RG8S)\
X(RG16)\
X(RG16I)\
X(RG16U)\
X(RG16F)\
X(RG16S)\
X(RG32I)\
X(RG32U)\
X(RG32F)\
X(RGB8)\
X(RGB8I)\
X(RGB8U)\
X(RGB8S)\
X(RGB9E5F)\
X(BGRA8)\
X(RGBA8)\
X(RGBA8I)\
X(RGBA8U)\
X(RGBA8S)\
X(RGBA16)\
X(RGBA16I)\
X(RGBA16U)\
X(RGBA16F)\
X(RGBA16S)\
X(RGBA32I)\
X(RGBA32U)\
X(RGBA32F)\
X(R5G6B5)\
X(RGBA4)\
X(RGB5A1)\
X(RGB10A2)\
X(RG11B10F)\
X(UnknownDepth)\
X(D16)\
X(D24)\
X(D24S8)\
X(D32)\
X(D16F)\
X(D24F)\
X(D32F)\
X(D0S8)

/*
Compressed format:
	BC1:		DXT1 R5G6B5A1
	BC2:		DXT3 R5G6B5A4
	BC3:		DXT5 R5G6B5A8
	BC4:		LATC1/ATI1 R8
	BC5:		LATC2/ATI2 RG8
	BC6H:		BC6H RGB16F
	BC7:		BC7 RGB 4-7 bits per color channel, 0-8 bits alpha
*/

// Pixel format for textures and images
enum pixeltype_t {
#define X(name) PIXEL_FMT_##name,
	PIXELFORMAT
#undef X
	PIXEL_FMT_MAX
};

enum encodingtype_t {
	TYPE_UINT,
	TYPE_INT,
	TYPE_UNORM,
	TYPE_SNORM,
	TYPE_FLOAT,
	TYPE_MAX,
};

typedef struct pixelblockinfo_s {
	uint8_t bits_per_pixel;
	uint8_t block_width;
	uint8_t block_height;
	uint8_t block_size;
	uint8_t min_X;
	uint8_t min_Y;
	uint8_t depth_bits;
	uint8_t stencil_bits;
	uint8_t r_bits;
	uint8_t g_bits;
	uint8_t b_bits;
	uint8_t a_bits;
	uint8_t encoding;
	uint8_t components;
} pixelblockinfo_t;

static const pixelblockinfo_t pixel_type_info[] = {
	//  +--------------------------------------------- bits per pixel
	//  |   +----------------------------------------- block width
	//  |   |  +-------------------------------------- block height
	//  |   |  |   +---------------------------------- block size
	//  |   |  |   |  +------------------------------- min blocks x
	//  |   |  |   |  |  +---------------------------- min blocks y
	//  |   |  |   |  |  |   +------------------------ depth bits
	//  |   |  |   |  |  |   |  +--------------------- stencil bits
	//  |   |  |   |  |  |   |  |   +---+---+---+----- r, g, b, a bits
	//  |   |  |   |  |  |   |  |   r   g   b   a  +-- encoding type
	//  |   |  |   |  |  |   |  |   |   |   |   |  |
	{   4,  4, 4,  8, 1, 1,  0, 0,  0,  0,  0,  0, TYPE_UNORM,	4 }, // BC1
	{   8,  4, 4, 16, 1, 1,  0, 0,  0,  0,  0,  0, TYPE_UNORM,	4 }, // BC2
	{   8,  4, 4, 16, 1, 1,  0, 0,  0,  0,  0,  0, TYPE_UNORM,	4 }, // BC3
	{   4,  4, 4,  8, 1, 1,  0, 0,  0,  0,  0,  0, TYPE_UNORM,	1 }, // BC4
	{   8,  4, 4, 16, 1, 1,  0, 0,  0,  0,  0,  0, TYPE_UNORM,	2 }, // BC5
	{   8,  4, 4, 16, 1, 1,  0, 0,  0,  0,  0,  0, TYPE_FLOAT,	3 }, // BC6H
	{   8,  4, 4, 16, 1, 1,  0, 0,  0,  0,  0,  0, TYPE_UNORM,	3 }, // BC7
	{   0,  0, 0,  0, 0, 0,  0, 0,  0,  0,  0,  0, TYPE_MAX,	0 }, // Unknown
	{   1,  8, 1,  1, 1, 1,  0, 0,  1,  0,  0,  0, TYPE_UNORM,	1 }, // R1
	{   8,  1, 1,  1, 1, 1,  0, 0,  0,  0,  0,  8, TYPE_UNORM,	1 }, // A8
	{   8,  1, 1,  1, 1, 1,  0, 0,  8,  0,  0,  0, TYPE_UNORM,	1 }, // R8
	{   8,  1, 1,  1, 1, 1,  0, 0,  8,  0,  0,  0, TYPE_INT,	1 }, // R8I
	{   8,  1, 1,  1, 1, 1,  0, 0,  8,  0,  0,  0, TYPE_UINT,	1 }, // R8U
	{   8,  1, 1,  1, 1, 1,  0, 0,  8,  0,  0,  0, TYPE_SNORM,  1 }, // R8S
	{  16,  1, 1,  2, 1, 1,  0, 0, 16,  0,  0,  0, TYPE_UNORM,	1 }, // R16
	{  16,  1, 1,  2, 1, 1,  0, 0, 16,  0,  0,  0, TYPE_INT,	1 }, // R16I
	{  16,  1, 1,  2, 1, 1,  0, 0, 16,  0,  0,  0, TYPE_UINT,	1 }, // R16U
	{  16,  1, 1,  2, 1, 1,  0, 0, 16,  0,  0,  0, TYPE_FLOAT,	1 }, // R16F
	{  16,  1, 1,  2, 1, 1,  0, 0, 16,  0,  0,  0, TYPE_SNORM,	1 }, // R16S
	{  32,  1, 1,  4, 1, 1,  0, 0, 32,  0,  0,  0, TYPE_INT,	1 }, // R32I
	{  32,  1, 1,  4, 1, 1,  0, 0, 32,  0,  0,  0, TYPE_UINT,	1 }, // R32U
	{  32,  1, 1,  4, 1, 1,  0, 0, 32,  0,  0,  0, TYPE_FLOAT,	1 }, // R32F
	{  16,  1, 1,  2, 1, 1,  0, 0,  8,  8,  0,  0, TYPE_UNORM,	2 }, // RG8
	{  16,  1, 1,  2, 1, 1,  0, 0,  8,  8,  0,  0, TYPE_INT,	2 }, // RG8I
	{  16,  1, 1,  2, 1, 1,  0, 0,  8,  8,  0,  0, TYPE_UINT,	2 }, // RG8U
	{  16,  1, 1,  2, 1, 1,  0, 0,  8,  8,  0,  0, TYPE_SNORM,	2 }, // RG8S
	{  32,  1, 1,  4, 1, 1,  0, 0, 16, 16,  0,  0, TYPE_UNORM,	2 }, // RG16
	{  32,  1, 1,  4, 1, 1,  0, 0, 16, 16,  0,  0, TYPE_INT,	2 }, // RG16I
	{  32,  1, 1,  4, 1, 1,  0, 0, 16, 16,  0,  0, TYPE_UINT,	2 }, // RG16U
	{  32,  1, 1,  4, 1, 1,  0, 0, 16, 16,  0,  0, TYPE_FLOAT,	2 }, // RG16F
	{  32,  1, 1,  4, 1, 1,  0, 0, 16, 16,  0,  0, TYPE_SNORM,	2 }, // RG16S
	{  64,  1, 1,  8, 1, 1,  0, 0, 32, 32,  0,  0, TYPE_INT,	2 }, // RG32I
	{  64,  1, 1,  8, 1, 1,  0, 0, 32, 32,  0,  0, TYPE_UINT,	2 }, // RG32U
	{  64,  1, 1,  8, 1, 1,  0, 0, 32, 32,  0,  0, TYPE_FLOAT,	2 }, // RG32F
	{  24,  1, 1,  3, 1, 1,  0, 0,  8,  8,  8,  0, TYPE_UNORM,	3 }, // RGB8
	{  24,  1, 1,  3, 1, 1,  0, 0,  8,  8,  8,  0, TYPE_INT,	3 }, // RGB8I
	{  24,  1, 1,  3, 1, 1,  0, 0,  8,  8,  8,  0, TYPE_UINT,	3 }, // RGB8U
	{  24,  1, 1,  3, 1, 1,  0, 0,  8,  8,  8,  0, TYPE_SNORM,	3 }, // RGB8S
	{  32,  1, 1,  4, 1, 1,  0, 0,  9,  9,  9,  5, TYPE_FLOAT,	3 }, // RGB9E5F
	{  32,  1, 1,  4, 1, 1,  0, 0,  8,  8,  8,  8, TYPE_UNORM,	4 }, // BGRA8
	{  32,  1, 1,  4, 1, 1,  0, 0,  8,  8,  8,  8, TYPE_UNORM,	4 }, // RGBA8
	{  32,  1, 1,  4, 1, 1,  0, 0,  8,  8,  8,  8, TYPE_INT,	4 }, // RGBA8I
	{  32,  1, 1,  4, 1, 1,  0, 0,  8,  8,  8,  8, TYPE_UINT,	4 }, // RGBA8U
	{  32,  1, 1,  4, 1, 1,  0, 0,  8,  8,  8,  8, TYPE_SNORM,	4 }, // RGBA8S
	{  64,  1, 1,  8, 1, 1,  0, 0, 16, 16, 16, 16, TYPE_UNORM,	4 }, // RGBA16
	{  64,  1, 1,  8, 1, 1,  0, 0, 16, 16, 16, 16, TYPE_INT,	4 }, // RGBA16I
	{  64,  1, 1,  8, 1, 1,  0, 0, 16, 16, 16, 16, TYPE_UINT,	4 }, // RGBA16U
	{  64,  1, 1,  8, 1, 1,  0, 0, 16, 16, 16, 16, TYPE_FLOAT,	4 }, // RGBA16F
	{  64,  1, 1,  8, 1, 1,  0, 0, 16, 16, 16, 16, TYPE_SNORM,	4 }, // RGBA16S
	{ 128,  1, 1, 16, 1, 1,  0, 0, 32, 32, 32, 32, TYPE_INT,	4 }, // RGBA32I
	{ 128,  1, 1, 16, 1, 1,  0, 0, 32, 32, 32, 32, TYPE_UINT,	4 }, // RGBA32U
	{ 128,  1, 1, 16, 1, 1,  0, 0, 32, 32, 32, 32, TYPE_FLOAT,	4 }, // RGBA32F
	{  16,  1, 1,  2, 1, 1,  0, 0,  5,  6,  5,  0, TYPE_UNORM,	3 }, // R5G6B5
	{  16,  1, 1,  2, 1, 1,  0, 0,  4,  4,  4,  4, TYPE_UNORM,	4 }, // RGBA4
	{  16,  1, 1,  2, 1, 1,  0, 0,  5,  5,  5,  1, TYPE_UNORM,	4 }, // RGB5A1
	{  32,  1, 1,  4, 1, 1,  0, 0, 10, 10, 10,  2, TYPE_UNORM,	4 }, // RGB10A2
	{  32,  1, 1,  4, 1, 1,  0, 0, 11, 11, 10,  0, TYPE_UNORM,	3 }, // RG11B10F
	{   0,  0, 0,  0, 0, 0,  0, 0,  0,  0,  0,  0, TYPE_MAX,	0 }, // UnknownDepth
	{  16,  1, 1,  2, 1, 1, 16, 0,  0,  0,  0,  0, TYPE_UNORM,	4 }, // D16
	{  24,  1, 1,  3, 1, 1, 24, 0,  0,  0,  0,  0, TYPE_UNORM,	4 }, // D24
	{  32,  1, 1,  4, 1, 1, 24, 8,  0,  0,  0,  0, TYPE_UNORM,	4 }, // D24S8
	{  32,  1, 1,  4, 1, 1, 32, 0,  0,  0,  0,  0, TYPE_UNORM,	4 }, // D32
	{  16,  1, 1,  2, 1, 1, 16, 0,  0,  0,  0,  0, TYPE_FLOAT,	4 }, // D16F
	{  24,  1, 1,  3, 1, 1, 24, 0,  0,  0,  0,  0, TYPE_FLOAT,	4 }, // D24F
	{  32,  1, 1,  4, 1, 1, 32, 0,  0,  0,  0,  0, TYPE_FLOAT,	4 }, // D32F
	{   8,  1, 1,  1, 1, 1,  0, 8,  0,  0,  0,  0, TYPE_UNORM,	4 }, // D0S8
};

static const char* pixel_format_names[] = {
#define X(name) #name,
	PIXELFORMAT
#undef X
};

inline pixelblockinfo_t pixel_block_format(enum pixeltype_t type) {
	return pixel_type_info[type];
}

inline const char* pixel_format_name(enum pixeltype_t type) {
	return pixel_format_names[type];
}

inline uint8_t bits_per_pixel(enum pixeltype_t type) {
	return pixel_type_info[type].bits_per_pixel;
}

inline bool is_color(enum pixeltype_t type) {
	return type > PIXEL_FMT_Unknown && type < PIXEL_FMT_UnknownDepth;
}

inline bool is_depth(enum pixeltype_t type) {
	return type > PIXEL_FMT_UnknownDepth && type < PIXEL_FMT_MAX;
}

inline bool is_compressed(enum pixeltype_t type) {
	return type < PIXEL_FMT_Unknown;
}