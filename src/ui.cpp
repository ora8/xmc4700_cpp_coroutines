#include "ui.hpp"
#include "lvgl.h"

namespace {
ui::Callbacks callbacks;
lv_obj_t *lbl_temp = nullptr;
lv_obj_t *lbl_hum = nullptr;
lv_obj_t *lbl_press = nullptr;
lv_obj_t *bar_hum = nullptr;
lv_obj_t *lbl_uptime = nullptr;
lv_obj_t *lbl_period = nullptr;

// LVGL-printf ohne Fließkomma: Wert mit `decimals` Nachkommastellen als Ganzzahlen
void set_fixed(lv_obj_t *label, float value, unsigned decimals, const char *unit) {
  std::int32_t scale = 1;
  for (unsigned i = 0; i < decimals; ++i) scale *= 10;
  const bool neg = value < 0;
  const auto v = std::int32_t((neg ? -value : value) * float(scale) + 0.5f);
  if (decimals == 0) {
    lv_label_set_text_fmt(label, "%s%d %s", neg ? "-" : "", int(v), unit);
  } else {
    lv_label_set_text_fmt(label, "%s%d.%0*d %s", neg ? "-" : "", int(v / scale), int(decimals),
                          int(v % scale), unit);
  }
}

void show_period(std::int32_t ms) { lv_label_set_text_fmt(lbl_period, "%d ms", int(ms)); }

void on_switch(lv_event_t *e) {
  auto *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
  if (callbacks.led_enable) callbacks.led_enable(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

void on_slider(lv_event_t *e) {
  auto *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
  const std::int32_t ms = lv_slider_get_value(slider);
  show_period(ms);
  if (callbacks.blink_period) callbacks.blink_period(std::uint32_t(ms));
}

lv_obj_t *small_label(lv_obj_t *parent, const char *text) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
  return l;
}
} // namespace

namespace ui {

void create(const Callbacks &cb, std::uint32_t blink_period_ms) noexcept {
  callbacks = cb;
  lv_display_t *disp = lv_display_get_default();
  lv_theme_t *theme = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                            lv_palette_main(LV_PALETTE_TEAL), true,
                                            &lv_font_montserrat_14);
  lv_display_set_theme(disp, theme);

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_pad_all(scr, 6, 0);
  lv_obj_set_style_pad_row(scr, 6, 0);
  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Kopfzeile: Titel links, Laufzeit rechts
  lv_obj_t *head = lv_obj_create(scr);
  lv_obj_remove_style_all(head);
  lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_t *title = lv_label_create(head);
  lv_label_set_text(title, LV_SYMBOL_HOME " XMC4700 + LVGL");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lbl_uptime = small_label(head, "0 s");
  lv_obj_align(lbl_uptime, LV_ALIGN_RIGHT_MID, 0, 0);

  // Messwerte
  lv_obj_t *card = lv_obj_create(scr);
  lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  small_label(card, "Temperatur");
  lbl_temp = lv_label_create(card);
  lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lbl_temp, lv_palette_lighten(LV_PALETTE_ORANGE, 1), 0);
  lv_label_set_text(lbl_temp, "--.-- \xC2\xB0" "C"); // °C
  lv_obj_align(lbl_temp, LV_ALIGN_TOP_LEFT, 0, 16);

  lv_obj_t *hum_title = small_label(card, "Feuchte");
  lv_obj_align(hum_title, LV_ALIGN_TOP_LEFT, 165, 0);
  lbl_hum = lv_label_create(card);
  lv_obj_set_style_text_font(lbl_hum, &lv_font_montserrat_20, 0);
  lv_label_set_text(lbl_hum, "-- %");
  lv_obj_align(lbl_hum, LV_ALIGN_TOP_LEFT, 165, 16);
  bar_hum = lv_bar_create(card);
  lv_obj_set_size(bar_hum, 120, 8);
  lv_bar_set_range(bar_hum, 0, 100);
  lv_obj_align(bar_hum, LV_ALIGN_TOP_LEFT, 165, 44);

  lv_obj_t *press_title = small_label(card, "Druck");
  lv_obj_align(press_title, LV_ALIGN_TOP_LEFT, 0, 58);
  lbl_press = lv_label_create(card);
  lv_obj_set_style_text_font(lbl_press, &lv_font_montserrat_20, 0);
  lv_label_set_text(lbl_press, "---- hPa");
  lv_obj_align(lbl_press, LV_ALIGN_TOP_LEFT, 60, 54);

  // Bedienung: LED-Schalter und Blinkintervall
  lv_obj_t *ctrl = lv_obj_create(scr);
  lv_obj_set_size(ctrl, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(ctrl, 8, 0);
  lv_obj_remove_flag(ctrl, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *led_lbl = lv_label_create(ctrl);
  lv_label_set_text(led_lbl, LV_SYMBOL_POWER " LED blinkt");
  lv_obj_align(led_lbl, LV_ALIGN_TOP_LEFT, 0, 6);
  lv_obj_t *sw = lv_switch_create(ctrl);
  lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_add_event_cb(sw, on_switch, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *slider = lv_slider_create(ctrl);
  lv_slider_set_range(slider, 100, 1000);
  lv_slider_set_value(slider, std::int32_t(blink_period_ms), LV_ANIM_OFF);
  lv_obj_set_width(slider, 200);
  lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 6, 46);
  lv_obj_add_event_cb(slider, on_slider, LV_EVENT_VALUE_CHANGED, nullptr);
  lbl_period = small_label(ctrl, "");
  lv_obj_align(lbl_period, LV_ALIGN_TOP_RIGHT, 0, 40);
  show_period(std::int32_t(blink_period_ms));
}

void set_sensor(bool ok, float temperature_c, float humidity_pct, float pressure_hpa) noexcept {
  if (!ok) {
    lv_label_set_text(lbl_temp, "BME280 fehlt");
    return;
  }
  set_fixed(lbl_temp, temperature_c, 2, "\xC2\xB0" "C");
  set_fixed(lbl_hum, humidity_pct, 1, "%");
  set_fixed(lbl_press, pressure_hpa, 1, "hPa");
  lv_bar_set_value(bar_hum, std::int32_t(humidity_pct + 0.5f), LV_ANIM_OFF);
}

void set_uptime(std::uint32_t seconds) noexcept {
  lv_label_set_text_fmt(lbl_uptime, LV_SYMBOL_REFRESH " %02u:%02u:%02u",
                        unsigned(seconds / 3600), unsigned(seconds / 60 % 60),
                        unsigned(seconds % 60));
}

} // namespace ui
