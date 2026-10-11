#pragma once

int i2s_rx_start_block(void);
void i2s_dma_init(void);

void i2s_test_rx(void);
void i2s_test_tx(void);

// ----------- Легаси -------------------
void I2S2_Init();
void I2S2_StartTransmitIT(void);
void I2S2_Callback();
