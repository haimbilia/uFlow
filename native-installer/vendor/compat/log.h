#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdbool.h>

void WUPI_putstr(const char *str);
void WUPI_putstr_overwrite(const char *str);
void WUPI_printTop();
void WUPI_resetScreen();
void WUPI_waitButton();

#define WUPI_Log(...) \
    do { \
        char _wupi_print_str[256]; \
        snprintf(_wupi_print_str, sizeof(_wupi_print_str), __VA_ARGS__); \
        WUPI_putstr(_wupi_print_str); \
    } while (0)

#define WUPI_Log_Overwrite(...) \
    do { \
        char _wupi_print_str[256]; \
        snprintf(_wupi_print_str, sizeof(_wupi_print_str), __VA_ARGS__); \
        WUPI_putstr_overwrite(_wupi_print_str); \
    } while (0)

#ifdef __cplusplus
}
#endif
