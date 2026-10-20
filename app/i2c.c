#include "i2c.h"
#include "stm32f4xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_i2c.h"

#define I2C_PORT          GPIOB
#define I2C_SCL_PIN       LL_GPIO_PIN_10
#define I2C_SDA_PIN       LL_GPIO_PIN_11
#define I2C_DEVICE        I2C2
#define I2C_SPEED_HZ      100000U
#define I2C_WAIT_LIMIT    100000U

#define I2C_OK             0
#define I2C_ERR_TIMEOUT   -1
#define I2C_ERR_NACK      -2
#define I2C_ERR_BUS       -3
#define I2C_ERR_ARLO      -4
#define I2C_ERR_ARGUMENT  -5

/* При ошибке переходим к общему завершению обмена. */
#define I2C_CHECK(operation)          \
    do                                \
    {                                 \
        status = (operation);         \
        if (status != I2C_OK)         \
        {                             \
            goto finish;              \
        }                             \
    } while (0)

/* ===== Вспомогательные функции ===== */

static void i2c_clear_errors(void)
{
    LL_I2C_ClearFlag_AF(I2C_DEVICE);
    LL_I2C_ClearFlag_BERR(I2C_DEVICE);
    LL_I2C_ClearFlag_ARLO(I2C_DEVICE);
    LL_I2C_ClearFlag_OVR(I2C_DEVICE);
}

static int8_t i2c_wait(volatile uint32_t *reg,
                       uint32_t mask, uint32_t expected)
{
    uint32_t timeout = I2C_WAIT_LIMIT;

    while (timeout--)
    {
        uint32_t errors = I2C_DEVICE->SR1;

        if (errors & I2C_SR1_ARLO)
        {
            return I2C_ERR_ARLO;
        }

        if (errors & (I2C_SR1_BERR | I2C_SR1_OVR))
        {
            return I2C_ERR_BUS;
        }

        if (errors & I2C_SR1_AF)
        {
            return I2C_ERR_NACK;
        }

        if ((*reg & mask) == expected)
        {
            return I2C_OK;
        }
    }

    return I2C_ERR_TIMEOUT;
}

static int8_t i2c_wait_flag(uint32_t flag)
{
    return i2c_wait(&I2C_DEVICE->SR1, flag, flag);
}

static int8_t i2c_start(uint8_t address)
{
    LL_I2C_GenerateStartCondition(I2C_DEVICE);

    int8_t status = i2c_wait_flag(I2C_SR1_SB);

    if (status != I2C_OK)
    {
        return status;
    }

    LL_I2C_TransmitData8(I2C_DEVICE, address);

    /* ADDR сбрасывает вызывающий код. */
    return i2c_wait_flag(I2C_SR1_ADDR);
}

/* ===== Инициализация ===== */

void i2c_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C2);

    LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_I2C2);
    LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_I2C2);

    LL_I2C_Disable(I2C_DEVICE);

    LL_I2C_InitTypeDef i2c = {0};

    i2c.PeripheralMode = LL_I2C_MODE_I2C;
    i2c.ClockSpeed = I2C_SPEED_HZ;
    i2c.DutyCycle = LL_I2C_DUTYCYCLE_2;
    i2c.OwnAddress1 = 0;
    i2c.TypeAcknowledge = LL_I2C_ACK;
    i2c.OwnAddrSize = LL_I2C_OWNADDRESS1_7BIT;

    if (LL_I2C_Init(I2C_DEVICE, &i2c) != SUCCESS)
    {
        return;
    }

    LL_I2C_DisableBitPOS(I2C_DEVICE);
    LL_I2C_Enable(I2C_DEVICE);
}

/* ===== Основная операция обмена ===== */

int8_t i2c_write_read(uint8_t dev_addr,
                      const uint8_t *write_buf, uint16_t write_len,
                      uint8_t *read_buf, uint16_t read_len,
                      uint16_t timeout_ms)
{
    (void)timeout_ms;

    if ((dev_addr > 0x7FU) ||
        ((write_len != 0U) && (write_buf == 0)) ||
        ((read_len != 0U) && (read_buf == 0)))
    {
        return I2C_ERR_ARGUMENT;
    }

    i2c_clear_errors();

    int8_t status = i2c_wait(&I2C_DEVICE->CR1, I2C_CR1_STOP, 0);

    if (status != I2C_OK)
    {
        return status;
    }

    status = i2c_wait(&I2C_DEVICE->SR2, I2C_SR2_BUSY, 0);

    if (status != I2C_OK)
    {
        return status;
    }

    LL_I2C_DisableBitPOS(I2C_DEVICE);
    LL_I2C_AcknowledgeNextData(I2C_DEVICE, LL_I2C_ACK);

    /* Запись. При обеих нулевых длинах выполняется ping. */
    if ((write_len != 0U) || (read_len == 0U))
    {
        I2C_CHECK(i2c_start((uint8_t)(dev_addr << 1U)));

        LL_I2C_ClearFlag_ADDR(I2C_DEVICE);

        for (uint16_t i = 0; i < write_len; ++i)
        {
            I2C_CHECK(i2c_wait_flag(I2C_SR1_TXE));

            LL_I2C_TransmitData8(I2C_DEVICE, write_buf[i]);
        }

        if (write_len != 0U)
        {
            I2C_CHECK(i2c_wait_flag(I2C_SR1_BTF));
        }
    }

    if (read_len == 0U)
    {
        LL_I2C_GenerateStopCondition(I2C_DEVICE);
        goto finish;
    }

    /* После записи это repeated START, без STOP между фазами. */
    I2C_CHECK(i2c_start((uint8_t)((dev_addr << 1U) | 1U)));

    uint32_t primask;

    if (read_len == 1U)
    {
        LL_I2C_AcknowledgeNextData(I2C_DEVICE, LL_I2C_NACK);

        primask = __get_PRIMASK();
        __disable_irq();

        LL_I2C_ClearFlag_ADDR(I2C_DEVICE);
        LL_I2C_GenerateStopCondition(I2C_DEVICE);

        __set_PRIMASK(primask);

        I2C_CHECK(i2c_wait_flag(I2C_SR1_RXNE));

        read_buf[0] = LL_I2C_ReceiveData8(I2C_DEVICE);
    }
    else if (read_len == 2U)
    {
        LL_I2C_EnableBitPOS(I2C_DEVICE);

        primask = __get_PRIMASK();
        __disable_irq();

        LL_I2C_ClearFlag_ADDR(I2C_DEVICE);
        LL_I2C_AcknowledgeNextData(I2C_DEVICE, LL_I2C_NACK);

        __set_PRIMASK(primask);

        I2C_CHECK(i2c_wait_flag(I2C_SR1_BTF));

        primask = __get_PRIMASK();
        __disable_irq();

        LL_I2C_GenerateStopCondition(I2C_DEVICE);

        read_buf[0] = LL_I2C_ReceiveData8(I2C_DEVICE);
        read_buf[1] = LL_I2C_ReceiveData8(I2C_DEVICE);

        __set_PRIMASK(primask);
    }
    else
    {
        LL_I2C_ClearFlag_ADDR(I2C_DEVICE);

        while (read_len > 3U)
        {
            I2C_CHECK(i2c_wait_flag(I2C_SR1_RXNE));

            *read_buf++ = LL_I2C_ReceiveData8(I2C_DEVICE);
            --read_len;
        }

        /* Завершение приёма последних трёх байтов. */
        I2C_CHECK(i2c_wait_flag(I2C_SR1_BTF));

        LL_I2C_AcknowledgeNextData(I2C_DEVICE, LL_I2C_NACK);
        *read_buf++ = LL_I2C_ReceiveData8(I2C_DEVICE);

        I2C_CHECK(i2c_wait_flag(I2C_SR1_BTF));

        primask = __get_PRIMASK();
        __disable_irq();

        LL_I2C_GenerateStopCondition(I2C_DEVICE);

        *read_buf++ = LL_I2C_ReceiveData8(I2C_DEVICE);
        *read_buf = LL_I2C_ReceiveData8(I2C_DEVICE);

        __set_PRIMASK(primask);
    }

finish:
    if (status != I2C_OK)
    {
        /* После потери арбитража STOP не формируем. */
        if (!LL_I2C_IsActiveFlag_ARLO(I2C_DEVICE) &&
            LL_I2C_IsActiveFlag_MSL(I2C_DEVICE))
        {
            LL_I2C_GenerateStopCondition(I2C_DEVICE);
        }

        i2c_clear_errors();
        return status;
    }

    status = i2c_wait(&I2C_DEVICE->CR1, I2C_CR1_STOP, 0);

    if (status == I2C_OK)
    {
        status = i2c_wait(&I2C_DEVICE->SR2, I2C_SR2_BUSY, 0);
    }

    if (status == I2C_OK)
    {
        LL_I2C_DisableBitPOS(I2C_DEVICE);
        LL_I2C_AcknowledgeNextData(I2C_DEVICE, LL_I2C_ACK);
    }

    return status;
}

/* ===== Остальной публичный API ===== */

int8_t i2c_master_tx(uint8_t dev_addr, const uint8_t *buffer,
                   uint16_t len, uint16_t timeout_ms)
{
    return i2c_write_read(dev_addr, buffer, len,
                          0, 0, timeout_ms);
}

int8_t i2c_master_rx(uint8_t dev_addr, uint8_t *buffer,
                   uint8_t len, uint16_t timeout_ms)
{
    if (len == 0U)
    {
        return I2C_OK;
    }

    return i2c_write_read(dev_addr, 0, 0,
                          buffer, len, timeout_ms);
}

int8_t i2c_ping(uint8_t dev_addr, uint16_t timeout_ms)
{
    return i2c_master_tx(dev_addr, 0, 0, timeout_ms);
}