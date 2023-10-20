/*==========================================================*/
/*						RESOURCE LOADING					*/
/*==========================================================*/
#pragma once
#include "graphics.h"

typedef struct {
	const char* filename;
	shadermacro_t* macros;
	uint32_t macrocount;
	const char* entrypointname;
} shaderstageloaddesc_t;

typedef struct {
	shaderstageloaddesc_t stages[SHADER_STAGE_COUNT];
	shadertarget_t target;
	const shaderconstant_t* constants;
	uint32_t constantcount;
} shaderloaddesc_t;

typedef struct {
	uint64_t buffersize;
	uint32_t buffercount;
	bool singlethreaded;
} resourceloaderdesc_t;