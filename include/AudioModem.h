#ifndef AUDIO_MODEM_H
#define AUDIO_MODEM_H

#include <Arduino.h>
#include <vector>
#include <cstdint>
#include <queue>

// Audio Modem Implementation
// Supports AFSK (Audio Frequency Shift Keying) and Bell 202
// For direct audio connection to HAM radio transceivers

class AudioModem {
public:
    enum class ModemType {
        BELL_202,   // Bell 202 (1200 baud, 1200/2200 Hz)
        AFSK_1200,  // AFSK 1200 baud
        AFSK_2400   // AFSK 2400 baud (future)
    };
    
    explicit AudioModem(ModemType type = ModemType::BELL_202);
    ~AudioModem();
    
    // Initialize audio modem
    bool begin(uint8_t rxPin, uint8_t txPin, uint32_t sampleRate = 8000);
    
    // Transmit data (modulate to audio)
    bool transmit(const uint8_t* data, size_t len);
    
    // Receive data (demodulate from audio)
    bool receive(std::vector<uint8_t>& output);
    
    // Process audio samples (call from interrupt or main loop)
    void processAudioSample(int16_t sample);
    
    // Get current modem status
    bool isTransmitting() const { return _transmitting; }
    bool isReceiving() const { return _receiving; }
    
    // Set modem parameters
    void setMarkFrequency(uint16_t freq) { _markFreq = freq; }
    void setSpaceFrequency(uint16_t freq) { _spaceFreq = freq; }
    void setBaudRate(uint16_t baud) { _baudRate = baud; }
    
private:
    // Simple Goertzel tone detector
    struct Goertzel {
        float s_prev = 0.0f;
        float s_prev2 = 0.0f;
        float coeff = 0.0f;
        void reset() { s_prev = s_prev2 = 0.0f; }
        void feed(float sample) {
            float s = sample + coeff * s_prev - s_prev2;
            s_prev2 = s_prev;
            s_prev = s;
        }
        float power() const { return s_prev2 * s_prev2 + s_prev * s_prev - coeff * s_prev * s_prev2; }
    };

    ModemType _type;
    uint8_t _rxPin;
    uint8_t _txPin;
    uint32_t _sampleRate;
    uint16_t _markFreq;
    uint16_t _spaceFreq;
    uint16_t _baudRate;
    uint16_t _samplesPerBit;
    uint8_t _ledcChannel;
    uint8_t _ledcResolution;
    
    bool _transmitting;
    bool _receiving;
    
    // Receive state
    std::vector<uint8_t> _rxBuffer;  // HDLC decoded frame bytes

    // Demodulation state
    Goertzel _goertzelMark;
    Goertzel _goertzelSpace;
    uint16_t _sampleCount;
    uint8_t _onesCount;
    bool _inFrame;
    uint8_t _rxCurrentByte;
    uint8_t _rxBitPos;
    uint8_t _lastNRZIState;
    bool _nrziInitialized;
    std::queue< std::vector<uint8_t> > _rxFrames;
    
    // Bit processing helpers
    void processDecodedBit(uint8_t bit);
    void finalizeFrame();
};

#endif // AUDIO_MODEM_H

