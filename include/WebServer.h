#ifndef WEBSERVER_MANAGER_H
#define WEBSERVER_MANAGER_H

#include "Config.h"
#include <Arduino.h>

class WebServerManager {
public:
    // Initialize the web UI / REST API (no-op when disabled)
    static void begin();
    static bool isStarted();

    // Call from main loop to service any async tasks
    static void loop();

    // Validate/read runtime JSON config. Saving is handled by the REST config endpoint.
    static bool loadConfigFromFS(const char* path = "/config.json");
};

#endif // WEBSERVER_MANAGER_H
