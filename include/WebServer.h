#ifndef WEBSERVER_MANAGER_H
#define WEBSERVER_MANAGER_H

#include "Config.h"
#include <Arduino.h>

class WebServerManager {
public:
    // Initialize the web UI / REST API (no-op when disabled)
    static void begin();

    // Call from main loop to service any async tasks
    static void loop();

    // Load/Save runtime JSON config (returns false if not implemented)
    static bool loadConfigFromFS(const char* path = "/config.json");
    static bool saveConfigToFS(const char* path = "/config.json");
};

#endif // WEBSERVER_MANAGER_H
