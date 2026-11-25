#pragma once

// Spectrum scanner interface between the screen (device-ui) and the radio (firmware, board option HAS_SPECTRUM).
// The firmware implements these functions; while a scan runs the mesh is paused.
//
// Sweep: the visible window is up to SPECTRUM_MAX_BINS columns of one step each. Each column holds the strongest
// reading inside its step (measured every 250 kHz with a 250 kHz filter; the 125 kHz step uses 125 kHz).
// FFT: 128 bins of ~977 Hz around one frequency (125 kHz wide), computed on the radio chip (its 256-point FFT
// covers 250 kHz, but the 62.5 kHz channel filter leaves only the middle part usable).

#include <stdint.h>

#define SPECTRUM_MAX_BINS 320
#define SPECTRUM_FFT_BINS 128
#define SPECTRUM_FFT_SPAN_HZ 125000UL
#define SPECTRUM_NO_DATA INT16_MIN

enum SpectrumMode : uint8_t {
    SPECTRUM_SWEEP = 0,
    SPECTRUM_FFT = 1,
};

enum SpectrumRange : uint8_t {
    SPECTRUM_RANGE_SUBGHZ = 0, // 150 - 1200 MHz (LF port)
    SPECTRUM_RANGE_HF = 1,     // 1500 - 2740 MHz (receive only, HF port)
};

inline uint32_t spectrumRangeLoKHz(uint8_t range)
{
    return range == SPECTRUM_RANGE_HF ? 1500000UL : 150000UL;
}
inline uint32_t spectrumRangeHiKHz(uint8_t range)
{
    return range == SPECTRUM_RANGE_HF ? 2740000UL : 1200000UL;
}
// number of columns in the window for a range and step
inline uint16_t spectrumColumns(uint8_t range, uint16_t stepKHz)
{
    uint32_t n = (spectrumRangeHiKHz(range) - spectrumRangeLoKHz(range)) / stepKHz;
    return n > SPECTRUM_MAX_BINS ? SPECTRUM_MAX_BINS : (uint16_t)n;
}
// clamp a window start so the whole window stays in the range
inline uint32_t spectrumClampStartKHz(uint8_t range, uint16_t stepKHz, int64_t startKHz)
{
    const int64_t lo = spectrumRangeLoKHz(range);
    const int64_t hi = (int64_t)spectrumRangeHiKHz(range) - (int64_t)spectrumColumns(range, stepKHz) * stepKHz;
    return (uint32_t)(startKHz < lo ? lo : (startKHz > hi ? hi : startKHz));
}

struct SpectrumFrame {
    uint8_t mode;      // SpectrumMode
    uint8_t range;     // SpectrumRange (sweep)
    bool active;       // radio is scanning, mesh paused
    uint16_t bins;     // valid entries in data[]
    uint32_t startHz;  // frequency of the low edge of bin 0
    uint32_t binHz;    // width of one bin
    uint32_t centerHz; // FFT: center frequency
    uint32_t sweeps;   // completed sweeps / FFT frames
    uint32_t sweepMs;  // duration of the last sweep
    uint16_t progress; // sweep: columns of the current sweep done so far
    int16_t peak;      // strongest reading of the last sweep / frame, 0.5 dB steps
    uint32_t peakHz;   // its frequency
    // values in 0.5 dB steps (dBm * 2), SPECTRUM_NO_DATA where not measured; FFT values are estimated dBm
    int16_t data[SPECTRUM_MAX_BINS];
};

// Sweep the window startKHz .. startKHz + columns * stepKHz (stepKHz 125, 250, 500 or 1000).
bool spectrumStartSweep(uint8_t range, uint16_t stepKHz, uint32_t startKHz);
// FFT around centerKHz.
bool spectrumStartFft(uint32_t centerKHz);
// Stop scanning and resume the mesh.
void spectrumStop(void);
// Copy the latest data. The scan stops by itself if this is not called for 15 s.
bool spectrumGetFrame(SpectrumFrame *frame);
