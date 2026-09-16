# ============================================================================
# cmake/libs.cmake — УНИВЕРСАЛЬНЫЙ МЕНЕДЖЕР БИБЛИОТЕК
# ============================================================================
# Этот файл подключается из корневого CMakeLists.txt:
#
#     include(cmake/libs.cmake)
#
# Он просматривает флаги из project.cfg и для каждого ВКЛЮЧЁННОГО модуля:
#   1. добавляет каталог библиотеки (add_subdirectory),
#   2. линкует её target к исполняемому файлу (${PROJECT_NAME}.elf).
#
# Каждая библиотека САМА собирает свои исходники и объявляет свой target
# (add_library(... STATIC ...) + target_include_directories(... PUBLIC ...))
# внутри lib/<Module>/CMakeLists.txt. Здесь же задаются флаги классов TinyUSB
# и применяются общие MCU-опции.
#
# ВАЖНО про порядок: этот файл подключается ПОСЛЕ add_executable, поэтому
# общий INTERFACE-target (mcu_base) и compile-опции применяются к уже
# созданному исполняемому файлу через target_link_libraries.
# ============================================================================

# ----------------------------------------------------------------------------
# 0. Корень каталога библиотек
# ----------------------------------------------------------------------------
set(LIBS_DIR ${CMAKE_SOURCE_DIR}/lib)

# ----------------------------------------------------------------------------
# 1. Общий MCU-каркас (опции + флаги семейства) для ВСЕХ библиотек
# ----------------------------------------------------------------------------
# Создаём INTERFACE-target: он НЕ компилируется сам, но "раздаёт" опции и
# определения любым target'ам, которые на него ссылаются через
# target_link_libraries(...). Так каждая библиотека получает те же CPU/FPU
# опции и MCU define, что и главный исполняемый файл — иначе arm-none-eabi
# собрал бы библиотеки без нужных -mcpu/-mfloat (ABI-несовместимость).
add_library(mcu_base INTERFACE)
target_include_directories(mcu_base INTERFACE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/drivers
)
target_compile_definitions(mcu_base INTERFACE ${MCU_FAMILY_FLAGS})
target_compile_options(mcu_base INTERFACE
    -mcpu=${CPU_CORE}
    -mthumb
    ${FPU_FLAGS}
    -ffunction-sections
    -fdata-sections
)

# Вспомогательная функция: применить MCU-каркас к библиотеке.
function(mcu_link TARGET)
    target_link_libraries(${TARGET} PRIVATE mcu_base)
endfunction()

# ----------------------------------------------------------------------------
# 2. Подключение библиотек по флагам project.cfg
# ----------------------------------------------------------------------------

# --- SEGGER RTT ---
if(USE_SEGGER)
    add_subdirectory(${LIBS_DIR}/SEGGER)
    target_link_libraries(${PROJECT_NAME}.elf PRIVATE segger_rtt)
    message(STATUS "LIB: SEGGER RTT enabled")
endif()

# --- TinyUSB ---
if(USE_TINYUSB)
    # Флаги классов из project.cfg: управляют и набором исходников в
    # библиотеке, и макросами CFG_TUD_* (через compile-определения), которые
    # видят и библиотека, и приложение (app/*.c, src/main.c).
    set(TINYUSB_CLASS_DEFS "")
    if(TINYUSB_CLASS_MSC)
        list(APPEND TINYUSB_CLASS_DEFS -DCFG_TUD_MSC=1)
    else()
        list(APPEND TINYUSB_CLASS_DEFS -DCFG_TUD_MSC=0)
    endif()
    if(TINYUSB_CLASS_CDC)
        list(APPEND TINYUSB_CLASS_DEFS -DCFG_TUD_CDC=1)
    else()
        list(APPEND TINYUSB_CLASS_DEFS -DCFG_TUD_CDC=0)
    endif()
    if(TINYUSB_CLASS_HID)
        list(APPEND TINYUSB_CLASS_DEFS -DCFG_TUD_HID=1)
    else()
        list(APPEND TINYUSB_CLASS_DEFS -DCFG_TUD_HID=0)
    endif()

    add_subdirectory(${LIBS_DIR}/tinyusb)

    # Один и тот же набор class-флагов должен попасть и в приложение
    # (usb_descriptors.c, msc_disk.c, main.c), чтобы стек и дескрипторы
    # были согласованы с библиотекой.
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ${TINYUSB_CLASS_DEFS})

    target_link_libraries(${PROJECT_NAME}.elf PRIVATE tinyusb)
    message(STATUS "LIB: TinyUSB enabled (MSC=${TINYUSB_CLASS_MSC} CDC=${TINYUSB_CLASS_CDC} HID=${TINYUSB_CLASS_HID})")
endif()

# --- FatFs ---
if(USE_FATFS)
    add_subdirectory(${LIBS_DIR}/FatFs)
    target_link_libraries(${PROJECT_NAME}.elf PRIVATE FatFs)
    message(STATUS "LIB: FatFs enabled")
endif()

# --- FreeRTOS ---
if(USE_FREERTOS)
    # Задел на будущее: когда появится модуль lib/FreeRTOS/CMakeLists.txt
    # под текущее семейство (Cortex-M4), раскомментировать строки ниже.
    # add_subdirectory(${LIBS_DIR}/FreeRTOS)
    # target_link_libraries(${PROJECT_NAME}.elf PRIVATE FreeRTOS)
    message(WARNING "USE_FREERTOS включён, но модуль lib/FreeRTOS ещё не портирован под ${TARGET_CHIP}; библиотека не подключена")
endif()
