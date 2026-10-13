
#ifndef LOG_H
#define LOG_H

#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN "\033[36m"
#define COLOR_WHITE "\033[37m"
#define COLOR_BRIGHT_BLACK "\033[90m"
#define COLOR_BRIGHT_RED "\033[91m"
#define COLOR_BRIGHT_GREEN "\033[92m"
#define COLOR_BRIGHT_YELLOW "\033[93m"
#define COLOR_BRIGHT_BLUE "\033[94m"
#define COLOR_BRIGHT_MAGENTA "\033[95m"
#define COLOR_BRIGHT_CYAN "\033[96m"
#define COLOR_BRIGHT_WHITE "\033[97m"
#define COLOR_ORANGE "\033[38;5;208m"

#define COLOR_RESET "\033[0m"

/* typedef enum{

    Print_No_Log =0,
    Print_Selected_Log=1,
    Print_All_Log = 2


}Log_enable_disable;

extern Log_enable_disable current_log_level;
 */

#include "esp_log.h"

// Define log levels
typedef enum {
    Print_None = 0,
    Print_Error,
    Print_Warning,
    Print_Selected_Log,
    Print_Debug_log,
    Print_All_log,
} log_level_t;

// Global variable to control current log level
extern log_level_t current_log_level;

// Custom logging macros
#define MY_LOGE(tag, fmt, ...)                                                                                                                                                                         \
    do {                                                                                                                                                                                               \
        if (current_log_level >= Print_Error)                                                                                                                                                          \
            ESP_LOGE(tag, fmt, ##__VA_ARGS__);                                                                                                                                                         \
    } while (0)

#define MY_LOGW(tag, fmt, ...)                                                                                                                                                                         \
    do {                                                                                                                                                                                               \
        if (current_log_level >= Print_Warning)                                                                                                                                                        \
            ESP_LOGW(tag, fmt, ##__VA_ARGS__);                                                                                                                                                         \
    } while (0)

#define MY_LOGI(tag, fmt, ...)                                                                                                                                                                         \
    do {                                                                                                                                                                                               \
        if (current_log_level >= Print_Selected_Log)                                                                                                                                                   \
            ESP_LOGI(tag, fmt, ##__VA_ARGS__);                                                                                                                                                         \
    } while (0)

#define MY_LOGD(tag, fmt, ...)                                                                                                                                                                         \
    do {                                                                                                                                                                                               \
        if (current_log_level >= Print_Debug_log)                                                                                                                                                      \
            ESP_LOGD(tag, fmt, ##__VA_ARGS__);                                                                                                                                                         \
    } while (0)

#define MY_LOGV(tag, fmt, ...)                                                                                                                                                                         \
    do {                                                                                                                                                                                               \
        if (current_log_level >= Print_All_log)                                                                                                                                                        \
            ESP_LOGV(tag, fmt, ##__VA_ARGS__);                                                                                                                                                         \
    } while (0)

#endif  // LOG_H