#include "APRS.h"
#include <cstdio>
#include <cstring>
#include <cmath>

// APRS Implementation

void APRS::encodeBase91(uint32_t value, char* output, size_t width) {
    for (size_t i = width; i > 0; --i) {
        output[i - 1] = (char)((value % 91) + 33);
        value /= 91;
    }
    output[width] = 0;
}

void APRS::degreesToAPRS(float degrees, bool isLatitude, char* output) {
    bool negative = degrees < 0;
    degrees = fabsf(degrees);
    int deg = (int)degrees;
    float minutes = (degrees - deg) * 60.0f;

    if (isLatitude) {
        snprintf(output, 9, "%02d%05.2f%c", deg, minutes, negative ? 'S' : 'N');
    } else {
        snprintf(output, 10, "%03d%05.2f%c", deg, minutes, negative ? 'W' : 'E');
    }
}

float APRS::aprsToDegrees(const char* data, bool isLatitude) {
    if (!data) return 0.0f;
    int deg = 0;
    float minutes = 0.0f;
    char hemisphere = 0;

    if (isLatitude) {
        // Format: DDMM.MMH
        if (strlen(data) < 8) return 0.0f;
        char degBuf[3] = {data[0], data[1], 0};
        deg = atoi(degBuf);
        char minBuf[6] = {data[2], data[3], data[4], data[5], data[6], 0};
        minutes = atof(minBuf);
        hemisphere = data[7];
    } else {
        // Format: DDDMM.MMH
        if (strlen(data) < 9) return 0.0f;
        char degBuf[4] = {data[0], data[1], data[2], 0};
        deg = atoi(degBuf);
        char minBuf[6] = {data[3], data[4], data[5], data[6], data[7], 0};
        minutes = atof(minBuf);
        hemisphere = data[8];
    }

    float result = deg + minutes / 60.0f;
    if (hemisphere == 'S' || hemisphere == 'W') result = -result;
    return result;
}

void APRS::formatPosition(const Position& pos, char* output, bool compressed) {
    if (compressed) {
        float latitude = fminf(fmaxf(pos.latitude, -90.0f), 90.0f);
        float longitude = fminf(fmaxf(pos.longitude, -180.0f), 180.0f);
        uint32_t latValue = (uint32_t)lroundf(380926.0f * (90.0f - latitude));
        uint32_t lonValue = (uint32_t)lroundf(190463.0f * (180.0f + longitude));
        char lat[5], lon[5];
        encodeBase91(latValue, lat, 4);
        encodeBase91(lonValue, lon, 4);

        if (pos.course > 0 || pos.speed > 0) {
            uint8_t course = (uint8_t)((pos.course % 360) / 4);
            uint16_t speed = pos.speed;
            uint8_t speedCode = 0;
            while (speed > 1 && speedCode < 89) {
                speed = (uint16_t)((speed + 1) / 1.08f);
                speedCode++;
            }
            snprintf(output, 16, "!%c%s%s%c%c%c ",
                     pos.symbolTable, lat, lon, pos.symbol,
                     (char)(course + 33), (char)(speedCode + 33));
        } else {
            snprintf(output, 16, "!%c%s%s%c  ",
                     pos.symbolTable, lat, lon, pos.symbol);
        }
        return;
    }
    // Uncompressed: !DDMM.MMN/DDDMM.MME[CSE/SPD
    char lat[10], lon[11];
    degreesToAPRS(pos.latitude, true, lat);
    degreesToAPRS(pos.longitude, false, lon);

    snprintf(output, 40, "!%s%c%s%c", lat, pos.symbolTable, lon, pos.symbol);

    // Append course/speed if nonzero
    if (pos.course > 0 || pos.speed > 0) {
        char ext[16];
        snprintf(ext, sizeof(ext), "%03u/%03u", pos.course % 360, pos.speed);
        strcat(output, ext);
    }
}

void APRS::formatWeather(const Weather& weather, char* output) {
    // APRS positionless weather: _DDD/SSSgGGGtTTTrRRRp...
    int windDir = ((int)weather.windDirection) % 360;
    int windSpd = (int)weather.windSpeed;
    int gust = (int)weather.gustSpeed;
    int temp = (int)weather.temperature;
    int rain1h = (int)(weather.rainfall1h * 100);
    int rain24h = (int)(weather.rainfall24h * 100);
    int humidity = (int)weather.humidity;
    int pressure = (int)(weather.pressure * 10);

    snprintf(output, 80, "_%03d/%03dg%03dt%03dr%03dp%03dh%02db%05d",
             windDir, windSpd, gust, temp, rain1h, rain24h,
             humidity % 100, pressure);
}

bool APRS::parsePosition(const char* data, Position& pos) {
    if (!data) return false;
    size_t len = strlen(data);
    // Expect: !DDMM.MMN/DDDMM.MME... or =DDMM.MMN/DDDMM.MME...
    if (len < 19) return false;
    const char* p = data;
    if (*p == '!' || *p == '=' || *p == '/') p++; // skip data type indicator

    // Latitude: 8 chars
    char latStr[9];
    memcpy(latStr, p, 8); latStr[8] = 0;
    pos.latitude = aprsToDegrees(latStr, true);
    p += 8;

    pos.symbolTable = *p++;

    // Longitude: 9 chars
    char lonStr[10];
    memcpy(lonStr, p, 9); lonStr[9] = 0;
    pos.longitude = aprsToDegrees(lonStr, false);
    p += 9;

    pos.symbol = *p++;

    // Try to parse course/speed: CCC/SSS
    if (strlen(p) >= 7 && p[3] == '/') {
        char cseBuf[4] = {p[0], p[1], p[2], 0};
        char spdBuf[4] = {p[4], p[5], p[6], 0};
        pos.course = (uint16_t)atoi(cseBuf);
        pos.speed = (uint16_t)atoi(spdBuf);
    }

    return true;
}

bool APRS::parseWeather(const char* data, Weather& weather) {
    if (!data) return false;
    const char* p = data;
    if (*p == '_') p++; // skip weather indicator

    // Parse wind direction (3 digits)
    if (strlen(p) < 7) return false;
    char buf[6];
    memcpy(buf, p, 3); buf[3] = 0;
    weather.windDirection = (float)atoi(buf);
    p += 3;
    if (*p == '/') p++;
    memcpy(buf, p, 3); buf[3] = 0;
    weather.windSpeed = (float)atoi(buf);
    p += 3;

    // Parse remaining fields by prefix letter
    while (*p) {
        char code = *p++;
        // Read numeric value (up to 5 digits)
        int val = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9' && digits < 5) {
            val = val * 10 + (*p - '0');
            p++;
            digits++;
        }
        switch (code) {
            case 'g': weather.gustSpeed = (float)val; break;
            case 't': weather.temperature = (float)val; break;
            case 'r': weather.rainfall1h = val / 100.0f; break;
            case 'p': weather.rainfall24h = val / 100.0f; break;
            case 'P': weather.rainfallSinceMidnight = val / 100.0f; break;
            case 'h': weather.humidity = (float)(val == 0 ? 100 : val); break;
            case 'b': weather.pressure = val / 10.0f; break;
            case 'L': case 'l': weather.luminosity = (float)val; break;
            default: break; // skip unknown
        }
    }
    return true;
}

bool APRS::encodePosition(const AX25::Address& source, const Position& pos,
                           const char* comment, std::vector<uint8_t>& output) {
    // Build AX.25 UI frame with APRS position payload
    AX25::Frame frame;
    frame.destination = AX25::Address("APZ001", 0); // APRS path destination
    memcpy(frame.source.callsign, source.callsign, 6);
    frame.source.ssid = source.ssid;
    frame.control = AX25::ControlType::U_UI;
    frame.pid = 0xF0; // No layer 3

    // Build info field: position report
    char posStr[60] = {0};
    formatPosition(pos, posStr, false);
    for (const char* p = posStr; *p; ++p) frame.info.push_back((uint8_t)*p);
    if (comment) {
        for (const char* p = comment; *p; ++p) frame.info.push_back((uint8_t)*p);
    }

    return AX25::encodeFrame(frame, output);
}

bool APRS::encodeWeather(const AX25::Address& source, const Weather& weather,
                          const char* comment, std::vector<uint8_t>& output) {
    AX25::Frame frame;
    frame.destination = AX25::Address("APZ001", 0);
    memcpy(frame.source.callsign, source.callsign, 6);
    frame.source.ssid = source.ssid;
    frame.control = AX25::ControlType::U_UI;
    frame.pid = 0xF0;

    char wxStr[80] = {0};
    formatWeather(weather, wxStr);
    for (const char* p = wxStr; *p; ++p) frame.info.push_back((uint8_t)*p);
    if (comment) {
        for (const char* p = comment; *p; ++p) frame.info.push_back((uint8_t)*p);
    }

    return AX25::encodeFrame(frame, output);
}

bool APRS::encodeMessage(const AX25::Address& source, const AX25::Address& destination,
                         const Message& msg, std::vector<uint8_t>& output) {
    AX25::Frame frame;
    frame.destination = AX25::Address("APZ001", 0);
    memcpy(frame.source.callsign, source.callsign, 6);
    frame.source.ssid = source.ssid;
    frame.control = AX25::ControlType::U_UI;
    frame.pid = 0xF0;

    // APRS message format: :ADDRESSEE:message text{ID
    char infoStr[100];
    snprintf(infoStr, sizeof(infoStr), ":%-9.9s:%s", msg.addressee, msg.text);
    if (msg.messageId > 0) {
        char idStr[8];
        snprintf(idStr, sizeof(idStr), "{%u", msg.messageId);
        strncat(infoStr, idStr, sizeof(infoStr) - strlen(infoStr) - 1);
    }
    for (const char* p = infoStr; *p; ++p) frame.info.push_back((uint8_t)*p);

    (void)destination; // destination is encoded in the addressee field
    return AX25::encodeFrame(frame, output);
}

bool APRS::encodeStatus(const AX25::Address& source, const char* status,
                        std::vector<uint8_t>& output) {
    AX25::Frame frame;
    frame.destination = AX25::Address("APZ001", 0);
    memcpy(frame.source.callsign, source.callsign, 6);
    frame.source.ssid = source.ssid;
    frame.control = AX25::ControlType::U_UI;
    frame.pid = 0xF0;

    // Status format: >status text
    frame.info.push_back('>');
    if (status) {
        for (const char* p = status; *p; ++p) frame.info.push_back((uint8_t)*p);
    }

    return AX25::encodeFrame(frame, output);
}

bool APRS::decodePacket(const uint8_t* data, size_t len, AX25::Frame& frame,
                        std::vector<uint8_t>& aprsData) {
    if (!AX25::decodeFrame(data, len, frame)) return false;
    // APRS data is in the info field
    aprsData = frame.info;
    return !aprsData.empty();
}
