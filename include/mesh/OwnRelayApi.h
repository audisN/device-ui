#pragma once

// Board option HAS_OWN_RELAY_API: the firmware reports when a neighbour rebroadcasts one of our own packets
// (proof of two-way contact with that neighbour). Used by the Mesh Detector.

#include <stdint.h>

struct OwnRelayHeard {
    uint32_t count;  // increments with every overheard rebroadcast of our packets
    uint8_t relay;   // last byte of the relaying node's number
    int16_t rssi;    // as we heard the rebroadcast
    float snr;
};

// Copy the latest event; returns false if the firmware has none.
bool ownRelayHeardGet(OwnRelayHeard *out);
