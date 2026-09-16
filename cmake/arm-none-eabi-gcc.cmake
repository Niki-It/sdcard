# 1. Сначала указываем, что это кросс-компиляция
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 2. ВАЖНО: не пытаться линковать при проверке компилятора
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

# 3. Пути к компилятору
set(TOOLCHAIN_PREFIX "arm-none-eabi-")

set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE ${TOOLCHAIN_PREFIX}size)

set(COMPILER_PATH "${ARM_PATH}/bin/arm-none-eabi-gcc.exe")
set(COMPILER_INCLUDE_PATH_1 "${ARM_PATH}/arm-none-eabi/include")
set(COMPILER_INCLUDE_PATH_2 "${ARM_PATH}/lib/gcc/arm-none-eabi/15.3.1/include")

# 5. Поиск библиотек
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)