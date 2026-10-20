#ifndef I2C_H
#define I2C_H

#include <stdint.h>

void i2c_init(void);

int8_t i2c_master_tx(uint8_t dev_addr, const uint8_t *buffer,
                   uint16_t len, uint16_t timeout_ms);

int8_t i2c_master_rx(uint8_t dev_addr, uint8_t *buffer,
                   uint8_t len, uint16_t timeout_ms);

int8_t i2c_ping(uint8_t dev_addr, uint16_t timeout_ms);

int8_t i2c_write_read(uint8_t dev_addr,
                      const uint8_t *write_buf, uint16_t write_len,
                      uint8_t *read_buf, uint16_t read_len,
                      uint16_t timeout_ms);

#endif