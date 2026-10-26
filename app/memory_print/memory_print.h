#pragma once 

#include "stdint.h"
#include "stdio.h"

#define LOG_BUFFER_SIZE (8U * 1024U)
#define LOG_MAX_LINES   2048U

const char *log_lines[LOG_MAX_LINES];