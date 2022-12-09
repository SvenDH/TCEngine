/*==========================================================*/
/*							LOGGING							*/
/*==========================================================*/
#pragma once
#include "core.h"

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
