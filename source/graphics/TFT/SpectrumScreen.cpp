#ifdef HAS_SPECTRUM

#include "graphics/view/TFT/SpectrumScreen.h"
#include "fonts.h"
#include "util/ILog.h"
#include <string.h>

SpectrumScreen *SpectrumScreen::current = nullptr;
uint8_t SpectrumScreen::mode = SPECTRUM_SWEEP;
uint8_t SpectrumScreen::range = SPECTRUM_RANGE_SUBGHZ;
uint16_t SpectrumScreen::stepKHz = 1000;
uint32_t SpectrumScreen::startKHz[2] = {790000, 1500000};
uint8_t SpectrumScreen::holdSec = 2;
bool SpectrumScreen::showMax = false;
uint32_t SpectrumScreen::fftCenterKHz = 869525;
int16_t SpectrumScreen::dbLow = -120;
int16_t SpectrumScreen::dbHigh = -40;

namespace
{
constexpr int W = 320;
constexpr int H_HEADER = 26;
constexpr int Y_FREQ = H_HEADER;                 // window edge frequencies, above the spectrum
constexpr int H_WF = 91;                         // waterfall
constexpr int Y_CHART = Y_FREQ + 12, H_CHART = 240 - H_WF - Y_CHART;
constexpr int Y_WF = Y_CHART + H_CHART;
constexpr int TAP_PX = 8; // finger movement below this is a tap

constexpr uint32_t C_BG = 0x0B0F14;
constexpr uint32_t C_PANEL = 0x161B22;
constexpr uint32_t C_GRID = 0x1F2933;
constexpr uint32_t C_TEXT = 0xC9D1D9;
constexpr uint32_t C_DIM = 0x8B949E;
constexpr uint32_t C_TEAL = 0x2EC4B6;
constexpr uint32_t C_TEAL_FILL = 0x0F3D3A;
constexpr uint32_t C_AMBER = 0xE3B341;
constexpr uint32_t C_RED = 0xF85149;

const char *const rangeMap[] = {"Sub-G 150-1200", "HF 1500-2740", ""};
const char *const stepMap[] = {"1 MHz", "500k", "250k", "125k", ""};
const uint16_t stepValues[] = {1000, 500, 250, 125};

inline uint16_t rgb565(uint32_t hex)
{
    return lv_color_to_u16(lv_color_hex(hex));
}

// waterfall palette: deep purple -> magenta -> orange -> yellow
uint16_t wfLut[64];
void buildLut(void)
{
    static const uint32_t stops[] = {0x0B0F14, 0x2B0F5F, 0x8C2981, 0xDE4968, 0xFE9F6D, 0xFCFDBF};
    const int nStops = sizeof(stops) / sizeof(stops[0]);
    for (int i = 0; i < 64; i++) {
        float t = i / 63.0f * (nStops - 1);
        int s = (int)t;
        if (s >= nStops - 1)
            s = nStops - 2;
        float f = t - s;
        lv_color_t a = lv_color_hex(stops[s]), b = lv_color_hex(stops[s + 1]);
        lv_color_t c = lv_color_make(a.red + (b.red - a.red) * f, a.green + (b.green - a.green) * f,
                                     a.blue + (b.blue - a.blue) * f);
        wfLut[i] = lv_color_to_u16(c);
    }
}

// "869.625" (MHz with kHz) or "869.5253" for the FFT
void formatHz(char *buf, size_t len, uint32_t hz, bool fine)
{
    if (fine) {
        const uint32_t h100 = (hz + 50) / 100; // 100 Hz units
        lv_snprintf(buf, len, "%u.%04u", (unsigned)(h100 / 10000), (unsigned)(h100 % 10000));
    } else {
        const uint32_t khz = (hz + 500) / 1000;
        lv_snprintf(buf, len, "%u.%03u", (unsigned)(khz / 1000), (unsigned)(khz % 1000));
    }
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t *makeButton(lv_obj_t *parent, const char *text, uint32_t bg, uint32_t border, uint32_t fg, lv_event_cb_t cb,
                     void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *l = makeLabel(b, &ui_font_montserrat_12, fg);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

lv_obj_t *makeButtonMatrix(lv_obj_t *parent, const char *const *map, int y, uint32_t selected, lv_event_cb_t cb, void *user)
{
    lv_obj_t *m = lv_buttonmatrix_create(parent);
    lv_buttonmatrix_set_map(m, map);
    lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(m, true);
    lv_buttonmatrix_set_button_ctrl(m, selected, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_set_pos(m, 6, y);
    lv_obj_set_size(m, W - 12, 28);
    lv_obj_set_style_pad_all(m, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(m, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(m, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(m, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(m, lv_color_hex(C_GRID), LV_PART_MAIN);
    lv_obj_set_style_bg_color(m, lv_color_hex(C_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(m, lv_color_hex(C_PANEL), LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, lv_color_hex(C_DIM), LV_PART_ITEMS);
    lv_obj_set_style_text_font(m, &ui_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(m, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, lv_color_hex(C_TEAL_FILL), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(m, lv_color_hex(C_TEAL), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(m, cb, LV_EVENT_VALUE_CHANGED, user);
    return m;
}

lv_obj_t *makeSlider(lv_obj_t *parent, int y, int value, lv_event_cb_t cb, void *user)
{
    lv_obj_t *s = lv_slider_create(parent);
    lv_slider_set_range(s, -130, 0);
    lv_slider_set_value(s, value, LV_ANIM_OFF);
    lv_obj_set_pos(s, 20, y);
    lv_obj_set_size(s, W - 40, 10);
    lv_obj_set_style_bg_color(s, lv_color_hex(C_GRID), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, lv_color_hex(C_TEAL), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, lv_color_hex(C_TEAL), LV_PART_KNOB);
    lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, user);
    return s;
}
} // namespace

void SpectrumScreen::open(lv_obj_t *returnScreen)
{
    if (current)
        return;
    current = new SpectrumScreen(returnScreen);
}

SpectrumScreen::SpectrumScreen(lv_obj_t *returnScreen) : returnScreen(returnScreen)
{
    memset(&frame, 0, sizeof(frame));
    buildLut();
    build();
    lv_screen_load(screen);
    requestScan();
    timer = lv_timer_create(onTimer, 100, this);
}

SpectrumScreen::~SpectrumScreen()
{
    if (timer)
        lv_timer_delete(timer);
    lv_obj_delete(screen);
    lv_free(chartBuf);
    lv_free(waterfallBuf);
}

uint16_t SpectrumScreen::columns(void) const
{
    return mode == SPECTRUM_FFT ? SPECTRUM_FFT_BINS : spectrumColumns(range, stepKHz);
}

void SpectrumScreen::build(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // header: settings, strongest signal, exit
    lv_obj_t *set = makeButton(screen, LV_SYMBOL_SETTINGS " Settings", C_PANEL, C_GRID, C_TEXT, onSettings, this);
    lv_obj_set_size(set, 84, 22);
    lv_obj_set_pos(set, 2, 2);

    peakLabel = makeLabel(screen, &ui_font_montserrat_12, C_AMBER);
    lv_obj_align(peakLabel, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *exit = makeButton(screen, LV_SYMBOL_POWER " Mesh", 0x3A1616, 0xF85149, 0xFFA198, onExit, this);
    lv_obj_set_size(exit, 70, 22);
    lv_obj_align(exit, LV_ALIGN_TOP_RIGHT, -2, 2);

    // spectrum
    chartBuf = (uint16_t *)lv_malloc(W * H_CHART * sizeof(uint16_t));
    chart = lv_canvas_create(screen);
    lv_canvas_set_buffer(chart, chartBuf, W, H_CHART, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(chart, 0, Y_CHART);
    for (int i = 0; i < W * H_CHART; i++)
        chartBuf[i] = rgb565(C_BG);

    // waterfall
    waterfallBuf = (uint16_t *)lv_malloc(W * H_WF * sizeof(uint16_t));
    waterfall = lv_canvas_create(screen);
    lv_canvas_set_buffer(waterfall, waterfallBuf, W, H_WF, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(waterfall, 0, Y_WF);
    clearWaterfall();

    // window edges and mode, small, on the spectrum
    for (int i = 0; i < 2; i++)
        edgeLabel[i] = makeLabel(screen, &lv_font_montserrat_10, C_DIM);
    lv_obj_align(edgeLabel[0], LV_ALIGN_TOP_LEFT, 2, Y_FREQ);
    lv_obj_align(edgeLabel[1], LV_ALIGN_TOP_RIGHT, -2, Y_FREQ);
    modeLabel = makeLabel(screen, &lv_font_montserrat_10, C_TEAL);
    lv_obj_align(modeLabel, LV_ALIGN_TOP_MID, 0, Y_FREQ);

    // touch area over spectrum and waterfall: swipe = move the window, tap = FFT / back
    lv_obj_t *touch = lv_obj_create(screen);
    lv_obj_remove_style_all(touch);
    lv_obj_set_pos(touch, 0, Y_CHART);
    lv_obj_set_size(touch, W, 240 - Y_CHART);
    lv_obj_add_flag(touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(touch, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN));
    lv_obj_add_event_cb(touch, onTouch, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(touch, onTouch, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(touch, onTouch, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(touch, onTouch, LV_EVENT_PRESS_LOST, this);

    buildSettings();
    updateLabels();
}

void SpectrumScreen::buildSettings(void)
{
    settings = lv_obj_create(screen);
    lv_obj_remove_flag(settings, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(settings, 0, 0);
    lv_obj_set_size(settings, W, 240);
    lv_obj_set_style_radius(settings, 0, 0);
    lv_obj_set_style_border_width(settings, 0, 0);
    lv_obj_set_style_pad_all(settings, 0, 0);
    lv_obj_set_style_bg_color(settings, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(settings, LV_OPA_COVER, 0);

    lv_obj_t *title = makeLabel(settings, &ui_font_montserrat_16, C_TEAL);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " Scan settings");
    lv_obj_set_pos(title, 8, 6);
    lv_obj_t *ok = makeButton(settings, "OK", C_TEAL_FILL, C_TEAL, C_TEAL, onSettingsDone, this);
    lv_obj_set_size(ok, 60, 24);
    lv_obj_align(ok, LV_ALIGN_TOP_RIGHT, -6, 3);

    lv_obj_t *l = makeLabel(settings, &ui_font_montserrat_12, C_DIM);
    lv_label_set_text(l, "Range (MHz)");
    lv_obj_set_pos(l, 8, 30);
    rangeMatrix = makeButtonMatrix(settings, rangeMap, 44, range, onRange, this);

    l = makeLabel(settings, &ui_font_montserrat_12, C_DIM);
    lv_label_set_text(l, "Step (one column)");
    lv_obj_set_pos(l, 8, 74);
    stepMatrix = makeButtonMatrix(settings, stepMap, 88, stepKHz == 125 ? 3 : (stepKHz == 250 ? 2 : (stepKHz == 500 ? 1 : 0)),
                                  onStep, this);

    highLabel = makeLabel(settings, &ui_font_montserrat_12, C_DIM);
    lv_obj_set_pos(highLabel, 8, 120);
    highSlider = makeSlider(settings, 138, dbHigh, onDbSlider, this);
    lowLabel = makeLabel(settings, &ui_font_montserrat_12, C_DIM);
    lv_obj_set_pos(lowLabel, 8, 154);
    lowSlider = makeSlider(settings, 172, dbLow, onDbSlider, this);

    // max hold: on/off and how long it holds (red line on the spectrum)
    maxButton = makeButton(settings, "MAX", C_PANEL, C_GRID, C_DIM, onMax, this);
    lv_obj_set_size(maxButton, 54, 24);
    lv_obj_set_pos(maxButton, 6, 196);
    styleMaxButton();
    holdLabel = makeLabel(settings, &ui_font_montserrat_12, C_DIM);
    lv_obj_set_pos(holdLabel, 70, 192);
    holdSlider = lv_slider_create(settings);
    lv_slider_set_range(holdSlider, 1, 20);
    lv_slider_set_value(holdSlider, holdSec, LV_ANIM_OFF);
    lv_obj_set_pos(holdSlider, 76, 214);
    lv_obj_set_size(holdSlider, W - 96, 10);
    lv_obj_set_style_bg_color(holdSlider, lv_color_hex(C_GRID), LV_PART_MAIN);
    lv_obj_set_style_bg_color(holdSlider, lv_color_hex(C_RED), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(holdSlider, lv_color_hex(C_RED), LV_PART_KNOB);
    lv_obj_add_event_cb(holdSlider, onHoldSlider, LV_EVENT_VALUE_CHANGED, this);
    updateSettingsLabels();

    lv_obj_add_flag(settings, LV_OBJ_FLAG_HIDDEN);
}

void SpectrumScreen::updateSettingsLabels(void)
{
    lv_label_set_text_fmt(highLabel, "Top of the scale: %d dBm", dbHigh);
    lv_label_set_text_fmt(lowLabel, "Bottom of the scale: %d dBm", dbLow);
    lv_label_set_text_fmt(holdLabel, "Max hold: %u s", (unsigned)holdSec);
}

int SpectrumScreen::valueToY(int16_t halfDb) const
{
    const int top = dbHigh * 2, bottom = dbLow * 2;
    int y = (top - halfDb) * (H_CHART - 1) / (top - bottom);
    return y < 0 ? 0 : (y > H_CHART - 1 ? H_CHART - 1 : y);
}

uint16_t SpectrumScreen::valueToColor(int16_t halfDb) const
{
    if (halfDb == SPECTRUM_NO_DATA)
        return wfLut[0];
    int i = (halfDb - dbLow * 2) * 63 / ((dbHigh - dbLow) * 2);
    return wfLut[i < 0 ? 0 : (i > 63 ? 63 : i)];
}

void SpectrumScreen::updateHold(void)
{
    const int n = frame.bins;
    const bool restart = lv_tick_elaps(holdStartMs) > holdSec * 1000u;
    if (restart)
        holdStartMs = lv_tick_get();
    for (int i = 0; i < n && i < SPECTRUM_MAX_BINS; i++) {
        const int16_t v = frame.data[i];
        if (restart || hold[i] == SPECTRUM_NO_DATA || (v != SPECTRUM_NO_DATA && v > hold[i]))
            hold[i] = v;
    }
}

void SpectrumScreen::resetHold(void)
{
    for (int i = 0; i < SPECTRUM_MAX_BINS; i++)
        hold[i] = SPECTRUM_NO_DATA;
    holdStartMs = lv_tick_get();
}

void SpectrumScreen::styleMaxButton(void)
{
    lv_obj_set_style_bg_color(maxButton, lv_color_hex(showMax ? 0x3A1616 : C_PANEL), 0);
    lv_obj_set_style_border_color(maxButton, lv_color_hex(showMax ? C_RED : C_GRID), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(maxButton, 0), lv_color_hex(showMax ? C_RED : C_DIM), 0);
}

void SpectrumScreen::onMax(lv_event_t *e)
{
    SpectrumScreen *self = static_cast<SpectrumScreen *>(lv_event_get_user_data(e));
    showMax = !showMax;
    self->styleMaxButton();
    self->resetHold();
    self->drawChart();
}

void SpectrumScreen::onHoldSlider(lv_event_t *e)
{
    SpectrumScreen *self = static_cast<SpectrumScreen *>(lv_event_get_user_data(e));
    holdSec = (uint8_t)lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
    self->updateSettingsLabels();
}

void SpectrumScreen::requestScan(void)
{
    resetHold();
    lastRequestMs = lv_tick_get();
    if (mode == SPECTRUM_FFT)
        spectrumStartFft(fftCenterKHz);
    else
        spectrumStartSweep(range, stepKHz, startKHz[range]);
}

void SpectrumScreen::onTimer(lv_timer_t *t)
{
    static_cast<SpectrumScreen *>(lv_timer_get_user_data(t))->update();
}

void SpectrumScreen::update(void)
{
    // something else loaded another screen (resync, alert): stop scanning and go away
    if (lv_screen_active() != screen) {
        close();
        return;
    }
    if (!spectrumGetFrame(&frame))
        return;
    // keep the display on while scanning: when it sleeps, the wake-up loads the main screen and ends the scan
    lv_display_trigger_activity(NULL);
    // the firmware stops by itself when nobody polls: ask again
    if (!frame.active && lv_tick_elaps(lastRequestMs) > 2000)
        requestScan();

    const bool matches = frame.active && frame.mode == mode &&
                         (mode == SPECTRUM_FFT ? frame.centerHz == fftCenterKHz * 1000
                                               : frame.range == range && frame.binHz == stepKHz * 1000u &&
                                                     frame.startHz == startKHz[range] * 1000);
    if (matches && (frame.sweeps != lastSweeps || frame.progress != lastProgress)) {
        if (frame.sweeps != lastSweeps && !pressed)
            drawWaterfallRow();
        lastSweeps = frame.sweeps;
        lastProgress = frame.progress;
        updateHold();
        drawChart();
    }
    updateLabels();
}

void SpectrumScreen::drawChart(void)
{
    const uint16_t bg = rgb565(C_BG), grid = rgb565(C_GRID), fill = rgb565(C_TEAL_FILL), line = rgb565(C_TEAL);
    for (int i = 0; i < W * H_CHART; i++)
        chartBuf[i] = bg;
    for (int db = (dbHigh / 10) * 10; db > dbLow; db -= 10) {
        uint16_t *row = chartBuf + valueToY(db * 2) * W;
        for (int x = 0; x < W; x += 2)
            row[x] = grid;
    }
    const int n = frame.bins ? frame.bins : 1;
    int prevY = -1, peakX = -1, peakY = H_CHART;
    int16_t peakV = SPECTRUM_NO_DATA;
    for (int x = 0; x < W; x++) {
        const int16_t v = frame.data[x * n / W];
        if (v == SPECTRUM_NO_DATA) {
            prevY = -1;
            continue;
        }
        const int y = valueToY(v);
        if (v > peakV) {
            peakV = v;
            peakX = x;
            peakY = y;
        }
        for (int yy = y + 1; yy < H_CHART; yy++)
            chartBuf[yy * W + x] = fill;
        const int y0 = prevY < 0 ? y : (prevY < y ? prevY : y), y1 = prevY < 0 ? y : (prevY > y ? prevY : y);
        for (int yy = y0; yy <= y1; yy++)
            chartBuf[yy * W + x] = line;
        prevY = y;
    }
    // max hold of the last 2 s
    if (showMax) {
        const uint16_t red = rgb565(C_RED);
        int prevH = -1;
        for (int x = 0; x < W; x++) {
            const int16_t h = hold[x * n / W];
            if (h == SPECTRUM_NO_DATA) {
                prevH = -1;
                continue;
            }
            const int y = valueToY(h);
            const int y0 = prevH < 0 ? y : (prevH < y ? prevH : y), y1 = prevH < 0 ? y : (prevH > y ? prevH : y);
            for (int yy = y0; yy <= y1; yy++)
                chartBuf[yy * W + x] = red;
            prevH = y;
        }
    }
    // triangle above the strongest column
    if (peakX >= 0) {
        const uint16_t amber = rgb565(C_AMBER);
        for (int r = 0; r < 6; r++) {
            const int y = peakY - 8 + r; // tip 2 px above the line
            if (y < 0 || y >= H_CHART)
                continue;
            const int half = 5 - r;
            for (int x = peakX - half; x <= peakX + half; x++)
                if (x >= 0 && x < W)
                    chartBuf[y * W + x] = amber;
        }
    }
    lv_obj_invalidate(chart);
}

void SpectrumScreen::drawWaterfallRow(void)
{
    memmove(waterfallBuf + W, waterfallBuf, W * (H_WF - 1) * sizeof(uint16_t));
    const int n = frame.bins ? frame.bins : 1;
    for (int x = 0; x < W; x++)
        waterfallBuf[x] = valueToColor(frame.data[x * n / W]);
    lv_obj_invalidate(waterfall);
}

// move the waterfall history sideways with the finger (px > 0: content to the right)
void SpectrumScreen::shiftWaterfall(int px)
{
    if (px == 0)
        return;
    const int a = px > 0 ? px : -px;
    for (int y = 0; y < H_WF; y++) {
        uint16_t *row = waterfallBuf + y * W;
        if (a >= W) {
            for (int x = 0; x < W; x++)
                row[x] = wfLut[0];
        } else if (px > 0) {
            memmove(row + a, row, (W - a) * sizeof(uint16_t));
            for (int x = 0; x < a; x++)
                row[x] = wfLut[0];
        } else {
            memmove(row, row + a, (W - a) * sizeof(uint16_t));
            for (int x = W - a; x < W; x++)
                row[x] = wfLut[0];
        }
    }
    lv_obj_invalidate(waterfall);
}

void SpectrumScreen::clearWaterfall(void)
{
    for (int i = 0; i < W * H_WF; i++)
        waterfallBuf[i] = wfLut[0];
    lv_obj_invalidate(waterfall);
}

void SpectrumScreen::updateLabels(void)
{
    char f[24], buf[48];
    const bool fft = mode == SPECTRUM_FFT;
    if (!frame.active) {
        lv_label_set_text(peakLabel, "starting ...");
    } else if (frame.peak == SPECTRUM_NO_DATA || frame.peakHz == 0) {
        lv_label_set_text(peakLabel, "scanning ...");
    } else {
        formatHz(f, sizeof(f), frame.peakHz, fft);
        lv_snprintf(buf, sizeof(buf), "%s MHz  %d dBm", f, frame.peak / 2);
        lv_label_set_text(peakLabel, buf);
    }
    lv_obj_align(peakLabel, LV_ALIGN_TOP_MID, 0, 6);

    uint32_t lo, hi;
    if (fft) {
        lo = fftCenterKHz * 1000 - SPECTRUM_FFT_SPAN_HZ / 2;
        hi = lo + SPECTRUM_FFT_SPAN_HZ;
        lv_label_set_text(modeLabel, "FFT  +-62.5 kHz  (tap: back)");
    } else {
        lo = startKHz[range] * 1000;
        hi = lo + columns() * stepKHz * 1000;
        lv_label_set_text(modeLabel, "");
    }
    formatHz(f, sizeof(f), lo, fft);
    lv_label_set_text(edgeLabel[0], f);
    formatHz(f, sizeof(f), hi, fft);
    lv_label_set_text(edgeLabel[1], f);
    lv_obj_align(edgeLabel[1], LV_ALIGN_TOP_RIGHT, -2, Y_FREQ);
    lv_obj_align(modeLabel, LV_ALIGN_TOP_MID, 0, Y_FREQ);
}

void SpectrumScreen::onTouch(lv_event_t *e)
{
    SpectrumScreen *self = static_cast<SpectrumScreen *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p = {0, 0};
    if (lv_indev_active())
        lv_indev_get_point(lv_indev_active(), &p);
    const bool fft = mode == SPECTRUM_FFT;

    if (code == LV_EVENT_PRESSED) {
        self->pressed = true;
        self->dragged = false;
        self->pressX = p.x;
        self->appliedPx = 0;
        self->pressStartKHz = fft ? fftCenterKHz : startKHz[range];
        return;
    }
    if (!self->pressed)
        return;

    const int dx = p.x - self->pressX;
    if (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) {
        if (!self->dragged && (dx > TAP_PX || dx < -TAP_PX))
            self->dragged = true;
        if (self->dragged) {
            int movedPx; // what the window really moved, in pixels
            if (fft) {
                // 250 kHz over 320 px, 1 kHz resolution
                const int32_t c = (int32_t)self->pressStartKHz - dx * (int32_t)(SPECTRUM_FFT_SPAN_HZ / 1000) / W;
                const uint32_t lo = spectrumRangeLoKHz(SPECTRUM_RANGE_SUBGHZ), hi = spectrumRangeHiKHz(SPECTRUM_RANGE_HF);
                fftCenterKHz = c < (int32_t)lo ? lo : (c > (int32_t)hi ? hi : (uint32_t)c);
                movedPx = ((int32_t)self->pressStartKHz - (int32_t)fftCenterKHz) * W / (int32_t)(SPECTRUM_FFT_SPAN_HZ / 1000);
            } else {
                const int n = self->columns();
                const int cols = dx * n / W; // whole columns
                startKHz[range] = spectrumClampStartKHz(range, stepKHz, (int64_t)self->pressStartKHz - (int64_t)cols * stepKHz);
                const int moved = ((int32_t)self->pressStartKHz - (int32_t)startKHz[range]) / (int32_t)stepKHz;
                movedPx = moved * W / n;
            }
            self->shiftWaterfall(movedPx - self->appliedPx);
            self->appliedPx = movedPx;
            // follow the finger, but do not restart the scan more than ~6 times a second
            if (code == LV_EVENT_RELEASED || lv_tick_elaps(self->lastDragRequestMs) > 150) {
                self->lastDragRequestMs = lv_tick_get();
                self->requestScan();
            }
            self->updateLabels();
        }
    }
    if (code == LV_EVENT_RELEASED && !self->dragged) {
        // tap: FFT around the tapped frequency, or back to the sweep
        if (fft) {
            mode = SPECTRUM_SWEEP;
        } else {
            const int n = self->columns();
            const int col = p.x * n / W;
            fftCenterKHz = startKHz[range] + col * stepKHz + stepKHz / 2;
            mode = SPECTRUM_FFT;
        }
        ILOG_INFO("Spectrum UI: %s at x=%d", mode == SPECTRUM_FFT ? "FFT" : "sweep", (int)p.x);
        self->clearWaterfall();
        self->lastSweeps = 0;
        self->requestScan();
        self->updateLabels();
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
        self->pressed = false;
}

void SpectrumScreen::onSettings(lv_event_t *e)
{
    SpectrumScreen *self = static_cast<SpectrumScreen *>(lv_event_get_user_data(e));
    lv_obj_clear_flag(self->settings, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(self->settings);
}

void SpectrumScreen::onSettingsDone(lv_event_t *e)
{
    SpectrumScreen *self = static_cast<SpectrumScreen *>(lv_event_get_user_data(e));
    lv_obj_add_flag(self->settings, LV_OBJ_FLAG_HIDDEN);
    mode = SPECTRUM_SWEEP;
    startKHz[range] = spectrumClampStartKHz(range, stepKHz, startKHz[range]);
    self->clearWaterfall();
    self->lastSweeps = 0;
    self->requestScan();
    self->drawChart();
    self->updateLabels();
}

void SpectrumScreen::onRange(lv_event_t *e)
{
    range = lv_buttonmatrix_get_selected_button((lv_obj_t *)lv_event_get_target(e)) == 1 ? SPECTRUM_RANGE_HF
                                                                                          : SPECTRUM_RANGE_SUBGHZ;
}

void SpectrumScreen::onStep(lv_event_t *e)
{
    const uint32_t b = lv_buttonmatrix_get_selected_button((lv_obj_t *)lv_event_get_target(e));
    const uint16_t newStep = stepValues[b < 4 ? b : 0];
    // keep the middle of the window where it is
    for (uint8_t r = 0; r < 2; r++) {
        const uint32_t mid = startKHz[r] + spectrumColumns(r, stepKHz) * stepKHz / 2;
        startKHz[r] = spectrumClampStartKHz(r, newStep, (int64_t)mid - (int64_t)spectrumColumns(r, newStep) * newStep / 2);
    }
    stepKHz = newStep;
}

void SpectrumScreen::onDbSlider(lv_event_t *e)
{
    SpectrumScreen *self = static_cast<SpectrumScreen *>(lv_event_get_user_data(e));
    lv_obj_t *s = (lv_obj_t *)lv_event_get_target(e);
    int v = lv_slider_get_value(s);
    if (s == self->highSlider) {
        if (v < dbLow + 10) {
            v = dbLow + 10;
            lv_slider_set_value(s, v, LV_ANIM_OFF);
        }
        dbHigh = v;
    } else {
        if (v > dbHigh - 10) {
            v = dbHigh - 10;
            lv_slider_set_value(s, v, LV_ANIM_OFF);
        }
        dbLow = v;
    }
    self->updateSettingsLabels();
}

void SpectrumScreen::onExit(lv_event_t *e)
{
    SpectrumScreen *self = static_cast<SpectrumScreen *>(lv_event_get_user_data(e));
    lv_screen_load(self->returnScreen);
    self->close();
}

void SpectrumScreen::close(void)
{
    if (closing)
        return;
    closing = true;
    spectrumStop();
    if (timer) {
        lv_timer_delete(timer);
        timer = nullptr;
    }
    // delete after the current event / timer callback has finished with our objects
    lv_async_call(
        [](void *p) {
            delete static_cast<SpectrumScreen *>(p);
            current = nullptr;
        },
        this);
}

#endif
