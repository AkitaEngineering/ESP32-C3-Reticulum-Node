#ifndef LOG_H
#define LOG_H

#include "Config.h" // for DEBUG_ENABLED
#include <Arduino.h>

// Logging levels
// 0 = none, 1 = error, 2 = warn, 3 = info, 4 = debug
#ifndef LOG_LEVEL
    #if DEBUG_ENABLED
        #define LOG_LEVEL 4
    #else
        #define LOG_LEVEL 2
    #endif
#endif

#define LOG_ERROR(...) do { if(LOG_LEVEL >= 1) { DebugSerial.print("[ERROR] "); DebugSerial.printf(__VA_ARGS__); DebugSerial.println(); } } while(0)
#define LOG_WARN(...)  do { if(LOG_LEVEL >= 2) { DebugSerial.print("[WARN]  "); DebugSerial.printf(__VA_ARGS__); DebugSerial.println(); } } while(0)
#define LOG_INFO(...)  do { if(LOG_LEVEL >= 3) { DebugSerial.print("[INFO]  "); DebugSerial.printf(__VA_ARGS__); DebugSerial.println(); } } while(0)
#define LOG_DEBUG(...) do { if(LOG_LEVEL >= 4) { DebugSerial.print("[DEBUG] "); DebugSerial.printf(__VA_ARGS__); DebugSerial.println(); } } while(0)

#define CHECK_ALLOC(p) do { if (!(p)) { LOG_ERROR("Out of memory: %s:%d", __FILE__, __LINE__); } } while(0)

#endif // LOG_H
