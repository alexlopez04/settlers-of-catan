#pragma once
// =============================================================================
// catan_log.h — Uniform logging macros (identical copy lives in board/).
//
//   [   12345] I [TAG ] message text
//
// Each level is compiled in/out with CATAN_LOG_LEVEL (0..4).
// =============================================================================

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

#ifndef CATAN_LOG_LEVEL
#define CATAN_LOG_LEVEL 3   // 0=off, 1=error, 2=warn, 3=info, 4=debug
#endif

#ifndef CATAN_LOG_BUF_SIZE
#define CATAN_LOG_BUF_SIZE 192
#endif

static inline void catan_log_emit_(char lvl, const char* tag,
                                   const char* fmt, ...) {
    char buf[CATAN_LOG_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    Serial.print('[');
    unsigned long ms = millis();
    if      (ms <       10UL) Serial.print(F("       "));
    else if (ms <      100UL) Serial.print(F("      "));
    else if (ms <     1000UL) Serial.print(F("     "));
    else if (ms <    10000UL) Serial.print(F("    "));
    else if (ms <   100000UL) Serial.print(F("   "));
    else if (ms <  1000000UL) Serial.print(F("  "));
    else if (ms < 10000000UL) Serial.print(F(" "));
    Serial.print(ms);
    Serial.print(F("] "));
    Serial.write((uint8_t)lvl);
    Serial.print(F(" ["));
    Serial.print(tag);
    Serial.print(F("] "));
    Serial.write((const uint8_t*)buf,
                 (size_t)((n < (int)sizeof(buf)) ? n : (int)sizeof(buf) - 1));
    Serial.println();
}

#if CATAN_LOG_LEVEL >= 1
#  define LOGE(tag, fmt, ...) catan_log_emit_('E', tag, fmt, ##__VA_ARGS__)
#else
#  define LOGE(tag, fmt, ...) ((void)0)
#endif
#if CATAN_LOG_LEVEL >= 2
#  define LOGW(tag, fmt, ...) catan_log_emit_('W', tag, fmt, ##__VA_ARGS__)
#else
#  define LOGW(tag, fmt, ...) ((void)0)
#endif
#if CATAN_LOG_LEVEL >= 3
#  define LOGI(tag, fmt, ...) catan_log_emit_('I', tag, fmt, ##__VA_ARGS__)
#else
#  define LOGI(tag, fmt, ...) ((void)0)
#endif
#if CATAN_LOG_LEVEL >= 4
#  define LOGD(tag, fmt, ...) catan_log_emit_('D', tag, fmt, ##__VA_ARGS__)
#else
#  define LOGD(tag, fmt, ...) ((void)0)
#endif
