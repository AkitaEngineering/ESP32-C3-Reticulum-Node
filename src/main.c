#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

void app_main(void) {
    printf("\n\nESP32-C3 ESP-IDF Minimal Test Booted!\n");
    int count = 0;
    while (1) {
        printf("Tick %d\n", count++);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
