#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>

#define LEVEL_DEBUG 0
#define LEVEL_INFO 1
#define LEVEL_WARN 2
#define LEVEL_ERROR 3

#define ENABLE_FFMPEG_LOG 0

void decoder_log(int level, const char* format, ...);
void ffmpegLogCallback(void* ptr, int level, const char* fmt, va_list vl);

void setLogLevel(int level);
#define LOG_DEBUG(fmt, ...) decoder_log(LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) decoder_log(LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) decoder_log(LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) decoder_log(LEVEL_ERROR, fmt, ##__VA_ARGS__)


#endif
