#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// Common Configuration
//--------------------------------------------------------------------+

// 1. Указываем семейство микроконтроллера (STM32F4)
#define CFG_TUSB_MCU                OPT_MCU_STM32F4

// 2. Указываем ОС. Если используете FreeRTOS, замените на OPT_OS_FREERTOS
#define CFG_TUSB_OS                 OPT_OS_NONE

// 3. Уровень отладки. 0 = выкл, 1 = ошибки, 2 = предупреждения, 3 = полная информация
// Рекомендуется поставить 2 или 3 на этапе настройки, чтобы видеть логи в UART
#define CFG_TUSB_DEBUG              0

//--------------------------------------------------------------------+
// Device Configuration
//--------------------------------------------------------------------+

// 4. Включаем режим устройства (Device)
#define CFG_TUD_ENABLED             1

// 5. Настройка порта. STM32F405 имеет ТОЛЬКО USB_OTG_FS, который всегда является портом 0.
// High-Speed (HS) здесь невозможен аппаратно.
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUD_MAX_SPEED           OPT_MODE_FULL_SPEED

// Настройка памяти для USB DMA (для FS на STM32F4 достаточно обычного выравнивания)
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------+
// Device Class Configuration
//--------------------------------------------------------------------+

// Размер конечной точки 0 (стандарт для Full-Speed)
#define CFG_TUD_ENDPOINT0_SIZE      64

// Включаем нужные классы (соответствует вашему usb_descriptors.c).
// Значения по умолчанию ЗАДАЮТСЯ ФЛАГАМИ project.cfg через CMake:
//   TINYUSB_CLASS_MSC/CDC/HID -> -DCFG_TUD_MSC=1/-DCFG_TUD_CDC=1/-DCFG_TUD_HID=0
// (см. cmake/libs.cmake). Здесь указаны только ЗАПАСНЫЕ значения на случай
// сборки без менеджера библиотек. #ifndef не переопределяет флаг из CMake.
#ifndef CFG_TUD_CDC
#define CFG_TUD_CDC                 1
#endif
#ifndef CFG_TUD_MSC
#define CFG_TUD_MSC                 1
#endif
#ifndef CFG_TUD_HID
#define CFG_TUD_HID                 0
#endif
#ifndef CFG_TUD_MIDI
#define CFG_TUD_MIDI                0
#endif
#ifndef CFG_TUD_VENDOR
#define CFG_TUD_VENDOR              0
#endif

//--------------------------------------------------------------------+
// Class Specific Buffers
//--------------------------------------------------------------------+

// CDC FIFO размеры. Для Full-Speed максимальный размер пакета = 64 байта.
#define CFG_TUD_CDC_RX_BUFSIZE      64
#define CFG_TUD_CDC_TX_BUFSIZE      64
#define CFG_TUD_CDC_EP_BUFSIZE      64

// MSC Buffer size. 
// ВАЖНО: Для Full-Speed максимальный размер Bulk-пакета равен 64 байта. 
// Значение 512 предназначено только для High-Speed и может вызвать ошибки или перерасход памяти на F405.
#define CFG_TUD_MSC_EP_BUFSIZE      64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */