#pragma once

// Signal finder interface between the screen (device-ui) and the radio (firmware, board option HAS_SPECTRUM).
// The mesh is paused while it runs.
//  - Energy: sweeps 433-950 MHz and 2100-2700 MHz and lists where something is transmitting and how strong.
//  - Packets: listens on the channels of known systems (Meshtastic, LoRaWAN and ExpressLRS on 433, 868, 915 MHz,
//    ExpressLRS 2.4 GHz) and lists what it hears. A LoRa header received with the system's sync word confirms it.

#include <stdint.h>

#define FINDER_MAX_SIGNALS 40

enum FinderMode : uint8_t {
    FINDER_MODE_ENERGY = 0,
    FINDER_MODE_PACKETS = 1,
};

enum FinderSystem : uint8_t {
    FINDER_SYS_NONE = 0,   // energy reading
    FINDER_SYS_MESHTASTIC, // LoRa, sync word 0x2B: 869.525 (EU_868), 906.875 (US), 433.875 MHz (EU_433)
    FINDER_SYS_LORAWAN,    // LoRa BW125, sync word 0x34: EU868, US915 and EU433 channels
    FINDER_SYS_ELRS868,    // LoRa BW500 hopping over 13 channels 863.275-869.575 MHz
    FINDER_SYS_ELRS24,     // LoRa BW812 hopping over 80 channels 2400.4-2479.4 MHz
    FINDER_SYS_ELRS915,    // LoRa BW500 hopping over 40 channels 903.5-926.9 MHz
    FINDER_SYS_ELRS433,    // LoRa BW500 hopping over 433.1 / 433.925 / 434.45 MHz
};

struct FinderSignal {
    uint32_t freqKHz;  // frequency (ELRS: centre of its band)
    uint32_t loKHz;    // energy: occupied range
    uint32_t hiKHz;
    uint16_t widthKHz; // energy: occupied width (250 kHz resolution)
    int16_t peak;      // strongest reading, 0.5 dB steps
    int16_t last;      // last reading, 0.5 dB steps
    uint16_t hits;     // sweeps / detections it was heard in
    uint16_t ageS;     // seconds since it was last heard
    uint8_t system;    // FinderSystem
    uint8_t sf;        // LoRa spreading factor
    uint16_t bwKHz;    // LoRa bandwidth
    uint8_t channels;  // ELRS: different channels it was heard on
    bool confirmed;    // a LoRa header with the system's sync word was received
};

struct FinderStatus {
    bool active;       // radio is scanning, mesh paused
    uint8_t mode;      // FinderMode
    uint8_t watching;  // packets: FinderSystem being listened to now
    uint32_t sweepKHz; // energy: where the sweep is now
    uint32_t cycles;   // full passes
    uint16_t count;    // signals in the list
};

// Start (or switch mode; the list starts over) / stop. It stops by itself if not polled for 15 s.
bool finderStart(uint8_t mode);
void finderStop(void);
// Copy the list, strongest first. Returns the number of entries.
uint16_t finderGetList(FinderSignal *list, uint16_t max, FinderStatus *status);
// Forget everything heard so far.
void finderClear(void);
