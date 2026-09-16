# 1. Создаем статическую библиотеку. 
# STATIC означает, что из этих .c файлов будет собран .a (архив) файл, 
# который потом прилинкуется к нашему main.
add_library(segger_rtt STATIC
    SEGGER_RTT.c
    SEGGER_RTT_printf.c
)

# 2. Указываем, где лежат заголовочные файлы (инклуды)
# PUBLIC означает: "Эти папки нужны и для сборки самой библиотеки segger_rtt, 
# И для любого проекта, который подключит эту библиотеку".
target_include_directories(segger_rtt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 3. Общие MCU-флаги (CPU, FPU, MCU define) — едины с основным проектом
target_link_libraries(segger_rtt PRIVATE mcu_base)
