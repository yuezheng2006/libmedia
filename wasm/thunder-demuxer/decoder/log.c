#include "log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/timeb.h>
#include <sys/time.h>
#include "libavformat/avformat.h"

// 为Emscripten环境添加struct tm定义和localtime_r函数
#ifdef __EMSCRIPTEN__
#ifndef _STRUCT_TM_DEFINED
#define _STRUCT_TM_DEFINED
struct tm {
    int tm_sec;     /* seconds (0 - 60) */
    int tm_min;     /* minutes (0 - 59) */
    int tm_hour;    /* hours (0 - 23) */
    int tm_mday;    /* day of month (1 - 31) */
    int tm_mon;     /* month of year (0 - 11) */
    int tm_year;    /* year - 1900 */
    int tm_wday;    /* day of week (Sunday = 0) */
    int tm_yday;    /* day of year (0 - 365) */
    int tm_isdst;   /* is summer time in effect? */
};
#endif

// 直接实现localtime_r，不依赖localtime
struct tm* localtime_r(const time_t* clock, struct tm* result) {
    time_t t = *clock;
    
    // 计算时间
    int days, secs, years;
    int month, monthday, weekday;
    int hours, minutes, seconds;
    
    // 计算从1970年1月1日以来的秒数
    secs = t % 86400;
    days = t / 86400;
    
    // 计算时分秒
    hours = secs / 3600;
    secs = secs % 3600;
    minutes = secs / 60;
    seconds = secs % 60;
    
    // 计算日期
    // 简化计算，不考虑闰年
    years = 1970 + days / 365;
    monthday = days % 365;
    month = monthday / 30;  // 简化
    monthday = monthday % 30 + 1;
    weekday = (days + 4) % 7;  // 1970-01-01是星期四
    
    result->tm_sec = seconds;
    result->tm_min = minutes;
    result->tm_hour = hours;
    result->tm_mday = monthday;
    result->tm_mon = month;
    result->tm_year = years - 1900;
    result->tm_wday = weekday;
    result->tm_yday = days % 365;
    result->tm_isdst = 0;
    
    return result;
}
#endif

#ifdef __cplusplus
extern "C" {
#endif


char *logLevelStr[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR"
};

int logLevel = LEVEL_DEBUG;

void setLogLevel(int level) {
    logLevel = level;
}

void decoder_log(int level, const char* format, ...) {

    if (level < logLevel) {
        return;
    }

    char szBuffer[1024] = { 0 };
    char szTime[32]		= { 0 };
    char *p				= NULL;
    int prefixLength	= 0;
    const char *tag		= "Decoder.c";
    struct tm tmTime;
    struct timeb tb;

    ftime(&tb);
    localtime_r(&tb.time, &tmTime);

    if (1) {
        int tmYear		= tmTime.tm_year + 1900;
        int tmMon		= tmTime.tm_mon + 1;
        int tmMday		= tmTime.tm_mday;
        int tmHour		= tmTime.tm_hour;
        int tmMin		= tmTime.tm_min;
        int tmSec		= tmTime.tm_sec;
        int tmMillisec	= tb.millitm;
        sprintf(szTime, "%d-%d-%d %d:%d:%d.%d", tmYear, tmMon, tmMday, tmHour, tmMin, tmSec, tmMillisec);
    }

    prefixLength = sprintf(szBuffer, "[%s][%s][%s] ", szTime, logLevelStr[level], tag);
    p = szBuffer + prefixLength;
    
    if (1) {
        va_list ap;
        va_start(ap, format);
        vsnprintf(p, 1024 - prefixLength, format, ap);
        va_end(ap);
    }

    printf("%s\n", szBuffer);
}

void ffmpegLogCallback(void* ptr, int level, const char* fmt, va_list vl) {
    static int printPrefix	= 1;
    static int count		= 0;
    static char prev[1024]	= { 0 };
    char line[1024]			= { 0 };
    static int is_atty;
    AVClass* avc = ptr ? *(AVClass**)ptr : NULL;
    if (level > AV_LOG_DEBUG) {
        return;
    }
    if(ENABLE_FFMPEG_LOG == 0) {
        return;
    }
    if(logLevel != LEVEL_DEBUG) {
        return;
    }

    line[0] = 0;

    if (printPrefix && avc) {
        if (avc->parent_log_context_offset) {
            AVClass** parent = *(AVClass***)(((uint8_t*)ptr) + avc->parent_log_context_offset);
            if (parent && *parent) {
                snprintf(line, sizeof(line), "[ffmpeg:%s @ %p] ", (*parent)->item_name(parent), parent);
            }
        }
        snprintf(line + strlen(line), sizeof(line) - strlen(line), "[%s @ %p] ", avc->item_name(ptr), ptr);
    }

    vsnprintf(line + strlen(line), sizeof(line) - strlen(line), fmt, vl);
    line[strlen(line) + 1] = 0;
    LOG_DEBUG("%s", line);
}

#ifdef __cplusplus
}
#endif