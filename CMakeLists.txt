cmake_minimum_required(VERSION 3.20)

# Настройка toolchain
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/arm-none-eabi-gcc.cmake)

# Загрузка конфигурации
include(project.cfg)

project(${PROJECT_NAME} C ASM)

# Включение STM32 настроек
include(cmake/stm32.cmake)

# Сбор исходников
file(GLOB_RECURSE SOURCES 
    "app/*.c"
    "core/*.c"      
    "drivers/*.c"
    "src/*.c"
)

# Создание исполняемого файла
add_executable(${PROJECT_NAME}.elf
    ${SOURCES}
    ${CMAKE_SOURCE_DIR}/core/${STARTUP_FILE}
)

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/drivers
    ${CMAKE_SOURCE_DIR}/src   
)

# Подключение библиотек
include(cmake/libs.cmake)

# Настройки компиляции
target_compile_options(${PROJECT_NAME}.elf PRIVATE
    -mcpu=${CPU_CORE}
    -mthumb
    ${FPU_FLAGS}
    -Wall
    -Wextra
    -Wno-unused-parameter
    -ffunction-sections
    -fdata-sections
)

# Флаги линковки
target_link_options(${PROJECT_NAME}.elf PRIVATE
    -T${CMAKE_SOURCE_DIR}/core/${LINKER_SCRIPT}
    --specs=nano.specs
    --specs=nosys.specs
    -mcpu=${CPU_CORE}
    -mthumb
    ${FPU_FLAGS}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,--start-group
    -lm
    -lc
    -lgcc
    -Wl,--end-group
)

# Генерация bin и hex
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -Oihex $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.bin
)

# Генерация VS Code конфигураций
configure_file(
    ${CMAKE_SOURCE_DIR}/.vscode/launch.json.in
    ${CMAKE_SOURCE_DIR}/.vscode/launch.json
    @ONLY
)

configure_file(
    ${CMAKE_SOURCE_DIR}/.vscode/c_cpp_properties.json.in
    ${CMAKE_SOURCE_DIR}/.vscode/c_cpp_properties.json
    @ONLY
)

message(STATUS "VS Code configurations generated for ${DEVICE_NAME}")