#include <stdio.h>

#ifndef NDEBUG
#define LOG(status, msg, ...)   \
    printf("[%s] ", status);    \
    printf(msg, ##__VA_ARGS__); \
    printf("\n")
#define ERROR(msg, ...) LOG("ERROR", msg, ##__VA_ARGS__)
#else
#define LOG(status, msg, ...)
#define ERROR(msg, ...)          \
    fprintf(stderr, "[ERROR] "); \
    fprintf(stderr, msg, ##__VA_ARGS)
#endif
