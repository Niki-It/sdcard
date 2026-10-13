#pragma once
#include <stdbool.h>

int i2s_rx_start_block(void);
void i2s_dma_init(void);

void i2s_test_rx(void);
void i2s_test_tx(void);


bool i2s_rx_has_error(void);