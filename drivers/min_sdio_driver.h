#ifndef MIN_SDIO_H
#define MIN_SDIO_H

#include "stm32f4xx.h" // Подключает базовые определения CMSIS, включая SDIO_TypeDef

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * Макросы конфигурации (аналоги LL_SDIO_...)
 * ---------------------------------------------------------------------------- */

/* Управление питанием (регистр POWER, биты [1:0]) */
#define MIN_SDIO_POWER_OFF              (0x00000000U)
#define MIN_SDIO_POWER_ON               (0x00000003U)

/* Настройка тактирования (регистр CLKCR) */
#define MIN_SDIO_CLOCK_EDGE_RISING      (0x00000000U)       // Бит 13 = 0
#define MIN_SDIO_CLOCK_EDGE_FALLING     (0x00002000U)       // Бит 13 = 1 (NEGEDGE)

#define MIN_SDIO_CLOCK_BYPASS_DISABLE   (0x00000000U)       // Бит 10 = 0
#define MIN_SDIO_CLOCK_BYPASS_ENABLE    (0x00000400U)       // Бит 10 = 1 (BYPASS)

#define MIN_SDIO_CLOCK_POWER_SAVE_DISABLE (0x00000000U)     // Бит 9 = 0
#define MIN_SDIO_CLOCK_POWER_SAVE_ENABLE  (0x00000200U)     // Бит 9 = 1 (PWRSAV)

#define MIN_SDIO_BUS_WIDE_1B            (0x00000000U)       // Бит 11 = 0
#define MIN_SDIO_BUS_WIDE_4B            (0x00000800U)       // Бит 11 = 1 (WIDBUS)

#define MIN_SDIO_HARDWARE_FLOW_CONTROL_DISABLE (0x00000000U)// Бит 14 = 0
#define MIN_SDIO_HARDWARE_FLOW_CONTROL_ENABLE  (0x00004000U)// Бит 14 = 1 (HWFC_EN)

#define MIN_SDIO_CLOCK_ENABLE           (0x00000100U)       // Бит 8 = 1 (CLKEN)

/* Маска для очистки настраиваемых битов в регистре CLKCR (биты 0-14) */
#define MIN_SDIO_CLKCR_CLEAR_MASK       (0x00007FFFU)

#define MIN_SDIO_CMD_NO_RESPONSE    (0x00000000U)
#define MIN_SDIO_CMD_SHORT_RESPONSE (0x00000040U) // Бит 6: CPSMEN=1, WAITRESP=01 (R1, R6, R7)
#define MIN_SDIO_CMD_LONG_RESPONSE  (0x000000C0U) // Бит 6,7: CPSMEN=1, WAITRESP=11 (R2)

/* ----------------------------------------------------------------------------
 * Структура инициализации
 * ---------------------------------------------------------------------------- */
typedef struct {
    uint32_t ClockEdge;           // Фронт тактового сигнала
    uint32_t ClockBypass;         // Обход делителя частоты
    uint32_t ClockPowerSave;      // Режим энергосбережения тактирования
    uint32_t BusWide;             // Ширина шины данных (1 или 4 бита)
    uint32_t HardwareFlowControl; // Аппаратный контроль потока
    uint32_t ClockDiv;            // Делитель частоты (0..255)
} MIN_SDIO_InitTypeDef;

/* ----------------------------------------------------------------------------
 * Прототипы функций
 * ---------------------------------------------------------------------------- */

uint32_t MIN_SDIO_SendCmd(SDIO_TypeDef *SDIOx, uint8_t cmd_index, uint32_t arg, uint32_t resp_type);
uint32_t MIN_SDIO_GetResponse(SDIO_TypeDef *SDIOx, uint32_t *response);

// Прототипы функций чтения/записи
// Возвращает 0 при успехе, 1 при ошибке команды, 2 при ошибке данных (CRC/Timeout)
uint32_t MIN_SDIO_ReadBlock(SDIO_TypeDef *SDIOx, uint32_t block_addr, uint32_t *buffer);
uint32_t MIN_SDIO_WriteBlock(SDIO_TypeDef *SDIOx, uint32_t block_addr, const uint32_t *buffer);

/**
 * @brief Инициализация периферии SDIO согласно переданной структуре
 * @param SDIOx Указатель на регистры SDIO (макрос SDIO)
 * @param SDIO_InitStruct Указатель на структуру настроек
 */
void MIN_SDIO_Init(SDIO_TypeDef *SDIOx, MIN_SDIO_InitTypeDef *SDIO_InitStruct);

/**
 * @brief Установка состояния питания SDIO
 * @param SDIOx Указатель на регистры SDIO
 * @param PowerState Состояние питания (MIN_SDIO_POWER_ON или MIN_SDIO_POWER_OFF)
 */
void MIN_SDIO_SetPowerState(SDIO_TypeDef *SDIOx, uint32_t PowerState);

/**
 * @brief Включение тактового сигнала на пине SDIO_CK
 * @param SDIOx Указатель на регистры SDIO
 */
void MIN_SDIO_EnableClock(SDIO_TypeDef *SDIOx);

#ifdef __cplusplus
}
#endif

#endif /* MIN_SDIO_H */