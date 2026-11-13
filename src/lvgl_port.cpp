#include "lvgl_port.hpp"
#include "executor.hpp" // millis()
#include "lvgl.h"

namespace {
Ili9341 *tft_ = nullptr;
Xpt2046 *touch_ = nullptr;
UartHw *log_ = nullptr;
std::uint8_t rotation_ = 1;

// Teilpuffer: LVGL zeichnet streifenweise hinein, flush_cb schickt jeden Streifen
// ans Display. Ein zweiter Puffer brächte nichts, weil die SPI-Übertragung blockiert.
constexpr std::uint32_t BUF_LINES = 40;
alignas(4) std::uint8_t draw_buf[320 * BUF_LINES * 2];

std::uint32_t tick_cb() { return millis(); }

void flush_cb(lv_display_t *disp, const lv_area_t *area, std::uint8_t *px_map) {
  const std::uint32_t pixels = lv_area_get_size(area);
  // LVGL legt RGB565 little-endian ab, das ILI9341 erwartet das High-Byte zuerst
  lv_draw_sw_rgb565_swap(px_map, pixels);
  tft_->write_pixels(std::int16_t(area->x1), std::int16_t(area->y1), std::int16_t(area->x2),
                     std::int16_t(area->y2), px_map, pixels * 2);
  lv_display_flush_ready(disp);
}

void touch_read_cb(lv_indev_t *, lv_indev_data_t *data) {
  static lv_point_t last{0, 0};
  static bool was_pressed = false;
  Xpt2046::Raw raw;
  if (touch_->touched() && touch_->read_raw(raw)) {
    const auto p = touch_->map(raw, rotation_);
    last.x = p.x;
    last.y = p.y;
    data->state = LV_INDEV_STATE_PRESSED;
    if (!was_pressed && log_) {
      // Zum Kalibrieren (README): Rohwerte einmal pro Berührung ausgeben
      log_->print("Touch roh x=");
      log_->print_int(raw.x);
      log_->print(" y=");
      log_->print_int(raw.y);
      log_->print(" z=");
      log_->print_int(raw.z);
      log_->print(" -> Bildschirm ");
      log_->print_int(p.x);
      log_->print(",");
      log_->print_int(p.y);
      log_->print("\n");
    }
    was_pressed = true;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
    was_pressed = false;
  }
  data->point = last;
}

void log_cb(lv_log_level_t, const char *buf) {
  if (log_) log_->print(buf);
}
} // namespace

namespace lvgl_port {

void init(Ili9341 &tft, Xpt2046 *touch, std::uint8_t rotation, UartHw *log) noexcept {
  tft_ = &tft;
  touch_ = touch;
  log_ = log;
  rotation_ = rotation;

  lv_init();
  lv_tick_set_cb(tick_cb);
  lv_log_register_print_cb(log_cb);

  lv_display_t *disp = lv_display_create(tft.width(), tft.height());
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, draw_buf, nullptr, sizeof draw_buf,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush_cb);

  // Standardgruppe: Slider und andere Widgets fragen beim Antippen den Gruppenzustand ab
  // (ohne Gruppe gibt LVGL eine Warnung "group != NULL" aus).
  lv_group_set_default(lv_group_create());

  if (touch_) {
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
  }
}

std::uint32_t handler() noexcept {
  const std::uint32_t next = lv_timer_handler();
  if (next < 1) return 1;
  if (next > 10) return 10; // auch LV_NO_TIMER_READY
  return next;
}

std::uint32_t refresh_now() noexcept {
  const auto t0 = millis();
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(nullptr);
  return millis() - t0;
}

void memory(std::uint32_t &free_bytes, std::uint32_t &max_used_bytes) noexcept {
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  free_bytes = std::uint32_t(mon.free_size);
  max_used_bytes = std::uint32_t(mon.max_used);
}

} // namespace lvgl_port
