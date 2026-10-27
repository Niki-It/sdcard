#pragma once 

#include "stdint.h"
#include "stdio.h"

#define SEGGER_LOG
// #define MEMORY_LOG
#if MEMORY_LOG
#define LOG_BUFFER_SIZE (8U * 1024U)
#define LOG_MAX_LINES   2048U

const char *log_lines[LOG_MAX_LINES];
#endif