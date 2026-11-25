#pragma once

#ifdef HAS_SPECTRUM

#include "lvgl.h"
#include "spectrum/SpectrumApi.h"

/**
 * Spectrum analyzer screen (320x240): spectrum and waterfall of a sweep window (one column per step, swipe to move
 * it). The mesh is paused while it scans.
 */
class SpectrumScreen
{
  public:
    // open the screen; exit returns to returnScreen
    static void open(lv_obj_t *returnScreen);

  private:
    SpectrumScreen(lv_obj_t *returnScreen);
    ~SpectrumScreen();

    void build(void);
    void buildSettings(void);
    void requestScan(void);
    void close(void);
    void update(void);
    void drawChart(void);
    void drawWaterfallRow(void);
    void shiftWaterfall(int px);
    void clearWaterfall(void);
    void updateLabels(void);
    void updateSettingsLabels(void);
    int valueToY(int16_t halfDb) const;
    uint16_t valueToColor(int16_t halfDb) const;
    uint16_t columns(void) const;
    void updateHold(void);
    void resetHold(void);
    void styleMaxButton(void);
    static void onMax(lv_event_t *e);
    static void onHoldSlider(lv_event_t *e);

    static void onExit(lv_event_t *e);
    static void onSettings(lv_event_t *e);
    static void onSettingsDone(lv_event_t *e);
    static void onRange(lv_event_t *e);
    static void onStep(lv_event_t *e);
    static void onDbSlider(lv_event_t *e);
    static void onTouch(lv_event_t *e);
    static void onTimer(lv_timer_t *t);

    lv_obj_t *returnScreen;
    lv_obj_t *screen = nullptr;
    lv_obj_t *peakLabel = nullptr;
    lv_obj_t *maxButton = nullptr;
    lv_obj_t *holdLabel = nullptr;
    lv_obj_t *holdSlider = nullptr;
    lv_obj_t *chart = nullptr;
    lv_obj_t *waterfall = nullptr;
    lv_obj_t *edgeLabel[2] = {};
    lv_obj_t *centerLabel = nullptr;
    lv_obj_t *settings = nullptr;
    lv_obj_t *rangeMatrix = nullptr;
    lv_obj_t *stepMatrix = nullptr;
    lv_obj_t *lowSlider = nullptr;
    lv_obj_t *highSlider = nullptr;
    lv_obj_t *lowLabel = nullptr;
    lv_obj_t *highLabel = nullptr;
    lv_timer_t *timer = nullptr;
    uint16_t *chartBuf = nullptr;
    uint16_t *waterfallBuf = nullptr;

    bool closing = false;
    uint32_t lastSweeps = 0;
    uint16_t lastProgress = 0;
    uint32_t lastRequestMs = 0;

    // touch: swipe moves the window
    bool pressed = false;
    bool dragged = false;
    int pressX = 0;
    uint32_t pressStartKHz = 0;  // sweep window start when the finger went down
    int appliedPx = 0;           // waterfall shift already applied during this drag
    uint32_t lastDragRequestMs = 0;

    SpectrumFrame frame;
    int16_t hold[SPECTRUM_MAX_BINS];
    uint32_t holdStartMs = 0;

    // settings, kept while the device runs
    static uint8_t range;
    static uint16_t stepKHz;
    static uint32_t startKHz[2]; // per range
    static int16_t dbLow, dbHigh;
    static bool showMax;
    static uint8_t holdSec;

    static SpectrumScreen *current;
};

#endif
