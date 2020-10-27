/*==========================================================*/
/*							LOGGING							*/
/*==========================================================*/
#pragma once
#include "tccore.h"

/* Logging functions: */
TC_ENUM(LogType, char) {
	LOG_ALL,
	LOG_INFO,
	LOG_DEBUG,
	LOG_WARNING,
	LOG_ERROR,
	LOG_NONE
};

#define TRACE(level, M, ...) fprintf(stdout, "[" #level "] " M " (%s:%d)\n", ##__VA_ARGS__, __FILE__, __LINE__)
