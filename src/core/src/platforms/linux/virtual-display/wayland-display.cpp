#include <core/virtual-display.hpp>
#include <gst/app/gstappsrc.h>
#include <helpers/logger.hpp>
#include <immer/vector_transient.hpp>

extern "C" {
#include <waylanddisplay.h>
}

namespace wolf::core::virtual_display {

struct WaylandState {
  WaylandDisplay display{};
  immer::vector<std::string> env{};
  immer::vector<std::string> graphic_devices{};
};

wl_state_ptr create_wayland_display(const immer::array<std::string> &input_devices, const std::string &render_node) {
  logs::log(logs::debug, "[WAYLAND] Creating wayland display");
  auto w_display = display_init(render_node.c_str());
  immer::vector_transient<std::string> final_devices;
  immer::vector_transient<std::string> final_env;

  for (const auto &device : input_devices) {
    display_add_input_device(w_display, device.c_str());
  }

  { // c-style get devices list
    auto n_devices = display_get_devices_len(w_display);
    const char *strs[n_devices];
    display_get_devices(w_display, strs, n_devices);
    for (int i = 0; i < n_devices; ++i) {
      final_devices.push_back(strs[i]);
    }
  }

  { // c-style get env list
    auto n_envs = display_get_envvars_len(w_display);
    const char *strs[n_envs];
    display_get_envvars(w_display, strs, n_envs);
    for (int i = 0; i < n_envs; ++i) {
      final_env.push_back(strs[i]);
    }
  }

  auto wl_state = new WaylandState{.display = w_display,
                                   .env = final_env.persistent(),
                                   .graphic_devices = final_devices.persistent()};
  return {wl_state, &destroy};
}

std::unique_ptr<GstCaps, decltype(&gst_caps_unref)> set_resolution(WaylandState &w_state,
                                                                   const wolf::core::api::DisplayMode &display_mode,
                                                                   const std::optional<gst_element_ptr> &app_src) {
  /* clang-format off */
  auto caps = gst_caps_new_simple("video/x-raw",
                                  "width", G_TYPE_INT, display_mode.width,
                                  "height", G_TYPE_INT, display_mode.height,
                                  "framerate", GST_TYPE_FRACTION, display_mode.refreshRate, 1,
                                  "format", G_TYPE_STRING, "RGBx",
                                  NULL);/* clang-format on */

  if (app_src) {
    gst_app_src_set_caps(GST_APP_SRC((app_src.value()).get()), caps);
  }

  auto video_info = gst_video_info_new();
  if (gst_video_info_from_caps(video_info, caps)) {
    display_set_video_info(w_state.display, video_info);
  } else {
    logs::log(logs::warning, "[WAYLAND] Unable to set video_info from caps");
  }

  gst_video_info_free(video_info);
  return {caps, gst_caps_unref};
}

immer::vector<std::string> get_devices(const WaylandState &w_state) {
  return w_state.graphic_devices;
}

immer::vector<std::string> get_env(const WaylandState &w_state) {
  return w_state.env;
}

GstBuffer *get_frame(WaylandState &w_state) {
  return display_get_frame(w_state.display);
}

static void destroy(WaylandState *w_state) {
  logs::log(logs::trace, "~WaylandState");
  display_finish(w_state->display);
}

} // namespace wolf::core::virtual_display