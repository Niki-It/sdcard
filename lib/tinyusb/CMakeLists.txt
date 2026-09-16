# ============================================================================
# lib/tinyusb/CMakeLists.txt — библиотека TinyUSB (device-стек, порт DWC2)
# ============================================================================
# Каждая библиотека сама собирает свои исходники. Здесь TinyUSB собирается
# как статическая библиотека `tinyusb` из:
#   * ядра device-стека (tusb.c, tusb_fifo.c, usbd.c),
#   * DCD-порта DWC2 под STM32 (dcd_dwc2.c + dwc2_common.c),
#   * классов, выбранных флагами project.cfg (TINYUSB_CLASS_MSC/CDC/HID).
#
# Host-стек и неиспользуемые классы НЕ компилируются вовсе (строгий режим:
# только то, что включено флагами). Классовые .c внутри защищены
# #if CFG_TUD_XXX, но мы дополнительно подаём в компиляцию ровно те файлы,
# что запрошены в project.cfg.
#
# Конфигурация стека (tusb_config.h) лежит в app/; включается через
# CFG_TUSB_CONFIG_FILE, поэтому библиотека не зависит от порядка include.
# ============================================================================

set(TINYUSB_SRC
    src/tusb.c
    src/common/tusb_fifo.c
    src/device/usbd.c
    src/portable/synopsys/dwc2/dcd_dwc2.c
    src/portable/synopsys/dwc2/dwc2_common.c
)

# --- Флаги классов из project.cfg (TINYUSB_CLASS_MSC/CDC/HID) ---
# Эти же макросы видят app/*.c и src/main.c: они распространяются как
# PUBLIC к любому target'у, который линкует `tinyusb` (главным образом —
# исполняемому файлу), поэтому стек, классы и дескрипторы всегда согласованы.
set(TINYUSB_CLASS_DEFS "")
if(TINYUSB_CLASS_MSC)
    list(APPEND TINYUSB_CLASS_DEFS -DCFG_TUD_MSC=1)
    list(APPEND TINYUSB_SRC src/class/msc/msc_device.c)
else()
    list(APPEND TINYUSB_CLASS_DEFS -DCFG_TUD_MSC=0)
endif()
if(TINYUSB_CLASS_CDC)
    list(APPEND TINYUSB_CLASS_DEFS -DCFG_TUD_CDC=1)
    list(APPEND TINYUSB_SRC src/class/cdc/cdc_device.c)
else()
    list(APPEND TINYUSB_CLASS_DEFS -DCFG_TUD_CDC=0)
endif()
if(TINYUSB_CLASS_HID)
    list(APPEND TINYUSB_CLASS_DEFS -DCFG_TUD_HID=1)
    list(APPEND TINYUSB_SRC src/class/hid/hid_device.c)
else()
    list(APPEND TINYUSB_CLASS_DEFS -DCFG_TUD_HID=0)
endif()

add_library(tinyusb STATIC ${TINYUSB_SRC})

# Include-пути TinyUSB. app/ нужен, чтобы стек нашёл tusb_config.h;
# core/ — для stm32f4xx.h (используется dwc2_stm32.h).
target_include_directories(tinyusb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/portable/synopsys/dwc2
)

# Флаги классов распространяются PUBLIC — линкующие получают те же CFG_TUD_*
target_compile_definitions(tinyusb PUBLIC ${TINYUSB_CLASS_DEFS})

# Точка входа конфигурации — наш app/tusb_config.h
target_include_directories(tinyusb PUBLIC ${CMAKE_SOURCE_DIR}/app)
target_compile_definitions(tinyusb PRIVATE CFG_TUSB_CONFIG_FILE="tusb_config.h")

# Общие MCU-флаги (CPU, FPU, STM32F405xx, HSE/HSI, LL) — едины со сборкой ядра
target_link_libraries(tinyusb PRIVATE mcu_base)
