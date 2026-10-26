#pragma once

#include <stdbool.h>
#include <stdint.h>

bool usb_stop(uint32_t now_ms);
void process_usb(bool idle, uint32_t now_ms);