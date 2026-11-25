#if defined(HAS_SPECTRUM) && defined(HAS_SIGNAL_FINDER)

#include "graphics/view/TFT/SignalFinderScreen.h"
#include "fonts.h"
#include <string.h>

SignalFinderScreen *SignalFinderScreen::current = nullptr;
uint8_t SignalFinderScreen::mode = FINDER_MODE_ENERGY;

namespace
{
constexpr int W = 320;
constexpr int H_HEADER = 26;
constexpr int H_STATUS = 14;

constexpr uint32_t C_BG = 0x0B0F14;
constexpr uint32_t C_PANEL = 0x161B22;
constexpr uint32_t C_GRID = 0x1F2933;
constexpr uint32_t C_TEXT = 0xC9D1D9;
constexpr uint32_t C_DIM = 0x8B949E;
constexpr uint32_t C_TEAL = 0x2EC4B6;
constexpr uint32_t C_AMBER = 0xE3B341;

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

// "869.525" MHz
void formatKHz(char *buf, size_t len, uint32_t khz)
{
    lv_snprintf(buf, len, "%u.%03u", (unsigned)(khz / 1000), (unsigned)(khz % 1000));
}

// "1.2M", "250k", "9.6k"
void formatRate(char *buf, size_t len, uint32_t v)
{
    if (v >= 1000000)
        lv_snprintf(buf, len, "%u.%uM", (unsigned)(v / 1000000), (unsigned)(v % 1000000 / 100000));
    else if (v >= 10000)
        lv_snprintf(buf, len, "%uk", (unsigned)(v / 1000));
    else
        lv_snprintf(buf, len, "%u.%uk", (unsigned)(v / 1000), (unsigned)(v % 1000 / 100));
}

// what the signal is, as short as the column allows
void describe(char *buf, size_t len, const FinderSignal &s)
{
    const char *ok = s.confirmed ? " " LV_SYMBOL_OK : "";
    switch (s.system) {
    case FINDER_SYS_MESHTASTIC:
        lv_snprintf(buf, len, "Meshtastic SF%u/%u%s", s.sf, s.bwKHz, ok);
        break;
    case FINDER_SYS_LORAWAN:
        lv_snprintf(buf, len, "LoRaWAN SF%u%s", s.sf, ok);
        break;
    case FINDER_SYS_ELRS868:
        lv_snprintf(buf, len, "ELRS 868 SF%u %uch", s.sf, s.channels);
        break;
    case FINDER_SYS_ELRS24:
        lv_snprintf(buf, len, "ELRS 2.4 SF%u %uch", s.sf, s.channels);
        break;
    case FINDER_SYS_ELRS915:
        lv_snprintf(buf, len, "ELRS 915 SF%u %uch", s.sf, s.channels);
        break;
    case FINDER_SYS_ELRS433:
        lv_snprintf(buf, len, "ELRS 433 SF%u %uch", s.sf, s.channels);
        break;
    default:
        buf[0] = 0;
        break;
    }
}

const char *systemName(uint8_t system)
{
    switch (system) {
    case FINDER_SYS_MESHTASTIC:
        return "Meshtastic";
    case FINDER_SYS_LORAWAN:
        return "LoRaWAN";
    case FINDER_SYS_ELRS868:
        return "ELRS 868";
    case FINDER_SYS_ELRS24:
        return "ELRS 2.4 GHz";
    case FINDER_SYS_ELRS915:
        return "ELRS 915";
    case FINDER_SYS_ELRS433:
        return "ELRS 433";
    default:
        return "";
    }
}
} // namespace

void SignalFinderScreen::open(lv_obj_t *returnScreen)
{
    if (current)
        return;
    current = new SignalFinderScreen(returnScreen);
}

SignalFinderScreen::SignalFinderScreen(lv_obj_t *returnScreen) : returnScreen(returnScreen)
{
    build();
    lv_screen_load(screen);
    lastRequestMs = lv_tick_get();
    finderStart(mode);
    timer = lv_timer_create(onTimer, 1000, this);
    update();
}

SignalFinderScreen::~SignalFinderScreen()
{
    if (timer)
        lv_timer_delete(timer);
    if (screen)
        lv_obj_delete(screen);
}

void SignalFinderScreen::build(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // header: mode (left), clear, exit to the mesh (right)
    static const char *const modeMap[] = {"Energy", "Packets", ""};
    lv_obj_t *m = lv_buttonmatrix_create(screen);
    lv_buttonmatrix_set_map(m, modeMap);
    lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(m, true);
    lv_buttonmatrix_set_button_ctrl(m, mode == FINDER_MODE_PACKETS ? 1 : 0, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_set_pos(m, 2, 1);
    lv_obj_set_size(m, 140, 24);
    lv_obj_set_style_pad_all(m, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(m, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(m, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(m, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(m, lv_color_hex(C_GRID), LV_PART_MAIN);
    lv_obj_set_style_bg_color(m, lv_color_hex(C_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(m, lv_color_hex(C_PANEL), LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, lv_color_hex(C_DIM), LV_PART_ITEMS);
    lv_obj_set_style_text_font(m, &ui_font_montserrat_12, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(m, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, lv_color_hex(0x0F3D3A), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(m, lv_color_hex(C_TEAL), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(m, onMode, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *clear = makeButton(screen, LV_SYMBOL_TRASH, C_PANEL, C_GRID, C_TEXT, onClear, this);
    lv_obj_set_size(clear, 36, 22);
    lv_obj_align(clear, LV_ALIGN_TOP_RIGHT, -76, 2);

    lv_obj_t *exit = makeButton(screen, LV_SYMBOL_POWER " Mesh", 0x3A1616, 0xF85149, 0xFFA198, onExit, this);
    lv_obj_set_size(exit, 70, 22);
    lv_obj_align(exit, LV_ALIGN_TOP_RIGHT, -2, 2);

    statusLabel = makeLabel(screen, &lv_font_montserrat_10, C_AMBER);
    lv_obj_set_pos(statusLabel, 6, H_HEADER);

    // the list: frequency, strength, width, what it is
    table = lv_table_create(screen);
    lv_obj_set_pos(table, 0, H_HEADER + H_STATUS);
    lv_obj_set_size(table, W, 240 - H_HEADER - H_STATUS);
    lv_table_set_column_count(table, 4);
    lv_table_set_column_width(table, 0, 74);
    lv_table_set_column_width(table, 1, 40);
    lv_table_set_column_width(table, 2, 50);
    lv_table_set_column_width(table, 3, 156);
    lv_obj_set_style_bg_color(table, lv_color_hex(C_BG), LV_PART_MAIN);
    lv_obj_set_style_border_width(table, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(table, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(table, lv_color_hex(C_BG), LV_PART_ITEMS);
    lv_obj_set_style_text_color(table, lv_color_hex(C_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(table, &ui_font_montserrat_12, LV_PART_ITEMS);
    lv_obj_set_style_border_color(table, lv_color_hex(C_GRID), LV_PART_ITEMS);
    lv_obj_set_style_border_width(table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_pad_top(table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(table, 3, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(table, 4, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(table, 2, LV_PART_ITEMS);
    lv_table_set_cell_value(table, 0, 0, "MHz");
    lv_table_set_cell_value(table, 0, 1, "dBm");
}

void SignalFinderScreen::onTimer(lv_timer_t *t)
{
    static_cast<SignalFinderScreen *>(lv_timer_get_user_data(t))->update();
}

void SignalFinderScreen::update(void)
{
    // something else loaded another screen (resync, alert): stop and go away
    if (lv_screen_active() != screen) {
        close();
        return;
    }
    FinderStatus st{};
    const uint16_t n = finderGetList(list, FINDER_MAX_SIGNALS, &st);
    // keep the display on while it runs: a wake-up from sleep loads the main screen and ends the search
    lv_display_trigger_activity(NULL);
    if (!st.active && lv_tick_elaps(lastRequestMs) > 2000) {
        lastRequestMs = lv_tick_get();
        finderStart(mode);
    }

    char f[16], buf[64];
    if (!st.active) {
        lv_label_set_text(statusLabel, "starting ...");
    } else if (st.mode == FINDER_MODE_PACKETS) {
        lv_snprintf(buf, sizeof(buf), "%u found, pass %u, listening: %s", n, (unsigned)st.cycles + 1, systemName(st.watching));
        lv_label_set_text(statusLabel, buf);
    } else {
        formatKHz(f, sizeof(f), st.sweepKHz);
        lv_snprintf(buf, sizeof(buf), "%u signals, sweep %u, at %s MHz", n, (unsigned)st.cycles + 1, f);
        lv_label_set_text(statusLabel, buf);
    }

    lv_table_set_cell_value(table, 0, 2, st.mode == FINDER_MODE_PACKETS ? "seen" : "width");
    lv_table_set_cell_value(table, 0, 3, st.mode == FINDER_MODE_PACKETS ? "system" : "last seen");
    lv_table_set_row_count(table, n + 1);
    for (uint16_t i = 0; i < n; i++) {
        const FinderSignal &s = list[i];
        formatKHz(f, sizeof(f), s.freqKHz);
        lv_table_set_cell_value(table, i + 1, 0, f);
        if (s.peak == INT16_MIN)
            lv_snprintf(buf, sizeof(buf), "-");
        else
            lv_snprintf(buf, sizeof(buf), "%d", s.peak / 2);
        lv_table_set_cell_value(table, i + 1, 1, buf);
        if (s.system == FINDER_SYS_NONE) {
            formatRate(buf, sizeof(buf), (uint32_t)s.widthKHz * 1000);
            lv_table_set_cell_value(table, i + 1, 2, buf);
            if (s.ageS < 2)
                lv_snprintf(buf, sizeof(buf), "now (%ux)", s.hits);
            else
                lv_snprintf(buf, sizeof(buf), "%us ago (%ux)", s.ageS, s.hits);
        } else {
            lv_snprintf(buf, sizeof(buf), "%ux", s.hits);
            lv_table_set_cell_value(table, i + 1, 2, buf);
            describe(buf, sizeof(buf), s);
            if (s.ageS > 30) {
                const size_t l = strlen(buf);
                lv_snprintf(buf + l, sizeof(buf) - l, " %us", s.ageS);
            }
        }
        lv_table_set_cell_value(table, i + 1, 3, buf);
    }
}

void SignalFinderScreen::onMode(lv_event_t *e)
{
    SignalFinderScreen *self = static_cast<SignalFinderScreen *>(lv_event_get_user_data(e));
    mode = lv_buttonmatrix_get_selected_button((lv_obj_t *)lv_event_get_target(e)) == 1 ? FINDER_MODE_PACKETS : FINDER_MODE_ENERGY;
    finderStart(mode); // starts over: the list belongs to the mode
    self->update();
}

void SignalFinderScreen::onClear(lv_event_t *e)
{
    finderClear();
    static_cast<SignalFinderScreen *>(lv_event_get_user_data(e))->update();
}

void SignalFinderScreen::onExit(lv_event_t *e)
{
    SignalFinderScreen *self = static_cast<SignalFinderScreen *>(lv_event_get_user_data(e));
    lv_screen_load(self->returnScreen);
    self->close();
}

void SignalFinderScreen::close(void)
{
    if (closing)
        return;
    closing = true;
    finderStop();
    if (timer) {
        lv_timer_delete(timer);
        timer = nullptr;
    }
    lv_async_call(
        [](void *p) {
            delete static_cast<SignalFinderScreen *>(p);
            current = nullptr;
        },
        this);
}

#endif
