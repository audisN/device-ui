#pragma once

#if defined(HAS_SPECTRUM) && defined(HAS_SIGNAL_FINDER)

#include "lvgl.h"
#include "spectrum/SignalFinderApi.h"

/**
 * Signal finder screen (320x240): the list of signals the radio hears in 433-950 MHz and 2100-2700 MHz with their
 * strength, width, recognised modulation and frequency hopping. The mesh is paused while it runs.
 */
class SignalFinderScreen
{
  public:
    static void open(lv_obj_t *returnScreen);

  private:
    SignalFinderScreen(lv_obj_t *returnScreen);
    ~SignalFinderScreen();

    void build(void);
    void update(void);
    void close(void);

    static void onExit(lv_event_t *e);
    static void onClear(lv_event_t *e);
    static void onMode(lv_event_t *e);
    static void onTimer(lv_timer_t *t);

    lv_obj_t *returnScreen;
    lv_obj_t *screen = nullptr;
    lv_obj_t *statusLabel = nullptr;
    lv_obj_t *table = nullptr;
    lv_timer_t *timer = nullptr;
    bool closing = false;
    uint32_t lastRequestMs = 0;
    FinderSignal list[FINDER_MAX_SIGNALS];

    static uint8_t mode; // FinderMode, kept while the device runs
    static SignalFinderScreen *current;
};

#endif
