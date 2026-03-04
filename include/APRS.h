#ifndef APRS_H
#define APRS_H

#include <Arduino.h>
#include <vector>
#include <cstdint>
#include "AX25.h"

// Enhanced APRS (Automatic Packet Reporting System) Implementation
// Supports position reporting, weather data, and messaging

class APRS {
public:
    // APRS Position Format
    struct Position {
        float latitude;      // Degrees (-90 to 90)
        float longitude;     // Degrees (-180 to 180)
        float altitude;      // Meters above sea level
        uint16_t course;     // Course in degrees (0-360)
        uint16_t speed;      // Speed in knots
        uint8_t symbolTable; // Symbol table ('/' or '\')
        char symbol;         // Symbol character
        
        Position() : latitude(0), longitude(0), altitude(0), 
                     course(0), speed(0), symbolTable('/'), symbol('[') {}
    };
    
    // APRS Weather Data
    struct Weather {
        float windDirection;  // Degrees (0-360)
        float windSpeed;       // MPH
        float gustSpeed;       // MPH
        float temperature;     // Fahrenheit
        float rainfall1h;      // Inches (last hour)
        float rainfall24h;     // Inches (last 24 hours)
        float rainfallSinceMidnight; // Inches
        float humidity;        // Percent (0-100)
        float pressure;        // Millibars
        float luminosity;      // LUX
        
        Weather() : windDirection(0), windSpeed(0), gustSpeed(0),
                    temperature(0), rainfall1h(0), rainfall24h(0),
                    rainfallSinceMidnight(0), humidity(0), pressure(0), luminosity(0) {}
    };
    
    // APRS Message
    struct Message {
        char addressee[9];    // 9-character addressee callsign
        char text[67];        // Message text (max 67 chars)
        uint16_t messageId;   // Message ID (optional)
        
        Message() : messageId(0) {
            memset(addressee, 0, 9);
            memset(text, 0, 67);
        }
    };
    
    // Encode position report
    static bool encodePosition(const AX25::Address& source, const Position& pos, 
                               const char* comment, std::vector<uint8_t>& output);
    
    // Encode weather report
    static bool encodeWeather(const AX25::Address& source, const Weather& weather,
                              const char* comment, std::vector<uint8_t>& output);
    
    // Encode message
    static bool encodeMessage(const AX25::Address& source, const AX25::Address& destination,
                             const Message& msg, std::vector<uint8_t>& output);
    
    // Encode status message
    static bool encodeStatus(const AX25::Address& source, const char* status,
                            std::vector<uint8_t>& output);
    
    // Decode APRS packet
    static bool decodePacket(const uint8_t* data, size_t len, AX25::Frame& frame,
                            std::vector<uint8_t>& aprsData);
    
    // Format position for APRS (compressed or uncompressed)
    static void formatPosition(const Position& pos, char* output, bool compressed = false);
    
    // Format weather for APRS
    static void formatWeather(const Weather& weather, char* output);
    
    // Parse position from APRS string
    static bool parsePosition(const char* data, Position& pos);
    
    // Parse weather from APRS string
    static bool parseWeather(const char* data, Weather& weather);
    
private:
    // Convert degrees to APRS format (DDMM.MM or DDMMSS)
    static void degreesToAPRS(float degrees, bool isLatitude, char* output);
    
    // Convert APRS format to degrees
    static float aprsToDegrees(const char* data, bool isLatitude);
};

#endif // APRS_H

