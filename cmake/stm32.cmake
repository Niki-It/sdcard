# ============================================================
# Универсальная настройка под выбранный TARGET_CHIP
# ============================================================

if(NOT DEFINED TARGET_CHIP)
    message(FATAL_ERROR "TARGET_CHIP не задан в project.cfg!")
endif()

# --- STM32F407VGT6 (Cortex-M4, FPU) ---
if(TARGET_CHIP STREQUAL "F407VGT6")
    set(MCU_FAMILY "STM32F4")
    set(MCU_DEFINE "STM32F407xx")
    set(CPU_CORE "cortex-m4")
    set(FPU_FLAGS -mfloat-abi=hard -mfpu=fpv4-sp-d16)
    set(DEFAULT_HSI 16000000)
    set(STARTUP_FILE "startup_stm32f407xx.s")
    set(LINKER_SCRIPT "STM32F407VGTX_FLASH.ld")
    set(SVD_FILE "STM32F407xx.svd")
    set(OPENOCD_TARGET "target/stm32f4x.cfg")
    set(DEVICE_NAME "STM32F407VG")

elseif(TARGET_CHIP STREQUAL "F405RGT6")
    set(MCU_FAMILY "STM32F4")
    set(MCU_DEFINE "STM32F405xx")
    set(CPU_CORE "cortex-m4")
    set(FPU_FLAGS -mfloat-abi=hard -mfpu=fpv4-sp-d16)
    set(DEFAULT_HSI 16000000)
    set(STARTUP_FILE "startup_stm32f405xx.s")
    set(LINKER_SCRIPT "STM32F405xx_FLASH.ld")
    set(SVD_FILE "STM32F405.svd")
    set(OPENOCD_TARGET "target/stm32f4x.cfg")
    set(DEVICE_NAME "STM32F405RG")

# --- STM32F103RET6 (Cortex-M3) ---
elseif(TARGET_CHIP STREQUAL "F103RET6")
    set(MCU_FAMILY "STM32F1")
    set(MCU_DEFINE "STM32F103xE")
    set(CPU_CORE "cortex-m3")
    set(FPU_FLAGS "-mfloat-abi=soft")
    set(DEFAULT_HSI 8000000)
    set(STARTUP_FILE "startup_stm32f103xe.s")
    set(LINKER_SCRIPT "STM32F103XE_FLASH.ld")
    set(SVD_FILE "STM32F103xx.svd")
    set(OPENOCD_TARGET "target/stm32f1x.cfg")
    set(DEVICE_NAME "STM32F103RE")

# --- STM32F103C8T6 (Cortex-M3) ---
elseif(TARGET_CHIP STREQUAL "F103C8T6")
    set(MCU_FAMILY "STM32F1")
    set(MCU_DEFINE "STM32F103xB")
    set(CPU_CORE "cortex-m3")
    set(FPU_FLAGS "-mfloat-abi=soft")
    set(DEFAULT_HSI 8000000)
    set(STARTUP_FILE "startup_stm32f103xb.s")
    set(LINKER_SCRIPT "STM32F103XB_FLASH.ld")
    set(SVD_FILE "STM32F103xx.svd")
    set(OPENOCD_TARGET "target/stm32f1x.cfg")
    set(DEVICE_NAME "STM32F103C8")

# --- STM32F072VBT6 (Cortex-M0) ---
elseif(TARGET_CHIP STREQUAL "F072VBT6")
    set(MCU_FAMILY "STM32F0")
    set(MCU_DEFINE "STM32F072xB")
    set(CPU_CORE "cortex-m0")
    set(FPU_FLAGS "-mfloat-abi=soft")
    set(DEFAULT_HSI 8000000)
    set(STARTUP_FILE "startup_stm32f072xb.s")
    set(LINKER_SCRIPT "STM32F072VB_FLASH.ld")
    set(SVD_FILE "STM32F0x2.svd")
    set(OPENOCD_TARGET "target/stm32f0x.cfg")
    set(DEVICE_NAME "STM32F072VB")

# --- STM32F072CBT6 (Cortex-M0) ---
elseif(TARGET_CHIP STREQUAL "F072CBT6")
    set(MCU_FAMILY "STM32F0")
    set(MCU_DEFINE "STM32F072xB")
    set(CPU_CORE "cortex-m0")
    set(FPU_FLAGS "-mfloat-abi=soft")
    set(DEFAULT_HSI 8000000)
    set(STARTUP_FILE "startup_stm32f072xb.s")
    set(LINKER_SCRIPT "STM32F072CB_FLASH.ld")
    set(SVD_FILE "STM32F0x2.svd")
    set(OPENOCD_TARGET "target/stm32f0x.cfg")
    set(DEVICE_NAME "STM32F072CB")

# --- STM32F030K6T6 (Cortex-M0) ---
elseif(TARGET_CHIP STREQUAL "F030K6T6")
    set(MCU_FAMILY "STM32F0")
    set(MCU_DEFINE "STM32F030x6")
    set(CPU_CORE "cortex-m0")
    set(FPU_FLAGS "-mfloat-abi=soft")
    set(DEFAULT_HSI 8000000)
    set(STARTUP_FILE "startup_stm32f030x6.s")
    set(LINKER_SCRIPT "STM32F030x6_FLASH.ld")
    set(SVD_FILE "STM32F0x0.svd")
    set(OPENOCD_TARGET "target/stm32f0x.cfg")
    set(DEVICE_NAME "STM32F030K6")

# --- STM32F030F4P6 (Cortex-M0) ---
elseif(TARGET_CHIP STREQUAL "F030F4P6")
    set(MCU_FAMILY "STM32F0")
    set(MCU_DEFINE "STM32F030x4")
    set(CPU_CORE "cortex-m0")
    set(FPU_FLAGS "-mfloat-abi=soft")
    set(DEFAULT_HSI 8000000)
    set(STARTUP_FILE "startup_stm32f030x4.s")
    set(LINKER_SCRIPT "STM32F030x4_FLASH.ld")
    set(SVD_FILE "STM32F0x0.svd")
    set(OPENOCD_TARGET "target/stm32f0x.cfg")
    set(DEVICE_NAME "STM32F030F4P")

else()
    message(FATAL_ERROR "Неизвестный TARGET_CHIP: ${TARGET_CHIP}")
endif()

# ============================================================
# Применение общих настроек
# ============================================================

add_definitions(-D${MCU_DEFINE})

# --- СПИСОК ОБЩИХ MCU-ФЛАГОВ, ПЕРЕДАВАЕМЫХ В БИБЛИОТЕКИ ---
# Каждая библиотека (см. cmake/libs.cmake и lib/*/CMakeLists.txt) добавляет
# эти флаги к своему target через target_compile_definitions().
set(MCU_FAMILY_FLAGS "")

# --- Выбор источника тактирования ---
if(DEFINED HSE_VALUE AND HSE_VALUE)
    add_definitions(-DHSE_VALUE=${HSE_VALUE})
    set(CLOCK_DEFINE "HSE_VALUE=${HSE_VALUE}")
    set(CLOCK_SOURCE "HSE = ${HSE_VALUE} Hz")
    list(APPEND MCU_FAMILY_FLAGS -DHSE_VALUE=${HSE_VALUE})
else()
    add_definitions(-DHSI_VALUE=${DEFAULT_HSI})
    set(CLOCK_DEFINE "HSI_VALUE=${DEFAULT_HSI}")
    set(CLOCK_SOURCE "HSI = ${DEFAULT_HSI} Hz")
    list(APPEND MCU_FAMILY_FLAGS -DHSI_VALUE=${DEFAULT_HSI})
endif()

# --- LL драйверы ---
set(LL_DRIVER_DEFINE "")
if(USE_FULL_LL_DRIVER)
    add_definitions(-DUSE_FULL_LL_DRIVER)
    set(LL_DRIVER_DEFINE "USE_FULL_LL_DRIVER")
    list(APPEND MCU_FAMILY_FLAGS -DUSE_FULL_LL_DRIVER)
endif()

# MCU define добавляется в общий список после тактирования/LL (порядок не важен)
list(APPEND MCU_FAMILY_FLAGS -D${MCU_DEFINE})

# --- Assert ---
if(USE_FULL_ASSERT)
    add_definitions(-DUSE_FULL_ASSERT)
    message(STATUS "Full assert enabled")
else()
    message(STATUS "Full assert disabled")
endif()

# --- Пути к компилятору (можно переопределить в project.cfg) ---
if(NOT DEFINED COMPILER_PATH)
    set(COMPILER_PATH "C:/Program Files/Arm/GNU Toolchain mingw-w64-x86_64-arm-none-eabi/bin/arm-none-eabi-gcc.exe")
endif()

if(NOT DEFINED COMPILER_INCLUDE_PATH_1)
    set(COMPILER_INCLUDE_PATH_1 "C:/Program Files/Arm/GNU Toolchain mingw-w64-x86_64-arm-none-eabi/arm-none-eabi/include")
endif()

if(NOT DEFINED COMPILER_INCLUDE_PATH_2)
    set(COMPILER_INCLUDE_PATH_2 "C:/Program Files/Arm/GNU Toolchain mingw-w64-x86_64-arm-none-eabi/lib/gcc/arm-none-eabi/15.3.1/include")
endif()

# --- Вывод информации ---
message(STATUS "========================================")
message(STATUS "Target chip:    ${TARGET_CHIP}")
message(STATUS "Device name:    ${DEVICE_NAME}")
message(STATUS "Target chip:    ${TARGET_CHIP}")
message(STATUS "MCU family:     ${MCU_FAMILY}")
message(STATUS "CPU core:       ${CPU_CORE}")
message(STATUS "Clock source:   ${CLOCK_SOURCE}")
message(STATUS "Startup:        ${STARTUP_FILE}")
message(STATUS "Linker:         ${LINKER_SCRIPT}")
message(STATUS "SVD file:       ${SVD_FILE}")
message(STATUS "OpenOCD target: ${OPENOCD_TARGET}")
message(STATUS "========================================")