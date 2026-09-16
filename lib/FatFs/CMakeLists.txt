# ============================================================================
# FatFs — файловая система (ядро, без физического драйвера диска)
# ============================================================================
# Собирается только ЯДРО FatFs (исходники лежат в src/):
#   src/ff.c          — основная реализация файловой системы (обязательно)
#   src/ffsystem.c    — прослойка ОС (включает ffsystem_baremetal.c /
#                       ffsystem_cmsis_os.c по #if-условиям; обе реализации
#                       защищены препроцессором — конфликта символов нет)
#   src/ff_gen_drv.c  — менеджер логических дисков (СТ-обёртка, даёт
#                       глобальную переменную disk, на которую ссылается
#                       diskio.c)
#   src/diskio.c      — слай функции дискового уровня: без привязанного
#                       драйвера (FATFS_LinkDriver не вызван) вызовы disk_*
#                       безопасно вернут STA_NOINIT/RES_ERROR
#   src/ffunicode.c   — таблицы Unicode (нужны только при FF_USE_LFN != 0,
#                       но включение не навредит: таблица в read-only памяти)
#
# НЕ включены: src/drivers/sd_diskio.c (драйвер SD/MMC через SPI — требует
# SPI-порт, отсутствующий в этом проекте), ffsystem_template.c (заготовка).
#
# Конфигурация: ffconf.h в КОРНЕ этого каталога — копия ST-шаблона
# (ffconf_template.h) с безопасными для F030 значениями (LFN выкл,
# reentrant выкл). ff.h ищет заголовок "ffconf.h" — поэтому корень
# добавляется в include-пути.
# ============================================================================

add_library(FatFs STATIC
    src/ff.c
    src/ffsystem.c
    src/ff_gen_drv.c
    src/diskio.c
    src/ffunicode.c
)

# Экспортируем наружу свои заголовки: src/ (ff.h, diskio.h, ff_gen_drv.h)
# и корень (ffconf.h — конфигурация модуля).
target_include_directories(FatFs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Общие MCU-флаги (CPU, FPU, MCU define) — едины с основным проектом
target_link_libraries(FatFs PRIVATE mcu_base)