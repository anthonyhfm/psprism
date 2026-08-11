#include "utility_data.hpp"
#include "host/host.hpp"
#include "stubs/ctrl/ctrl_state.hpp"
#include "stubs/io/io_state.hpp"

#include <cstdint>
#include <string>
#include <vector>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) return __LINE__;                                         \
  } while (false)

int main() {
  const auto raw_view = io_state::parse_raw_disc_view(
      "disc0:/sce_lbn0x10_size0x40000");
  CHECK(raw_view.has_value());
  CHECK(raw_view->base == 0x8000U);
  CHECK(raw_view->size == 0x40000U);
  CHECK(io_state::parse_raw_disc_view(
            "umd0:/sce_lbn0x20_size0x1234") ==
        std::optional<io_state::FileView>({0x10000U, 0x1234U}));
  CHECK(!io_state::parse_raw_disc_view("disc0:/PSP_GAME/SYSDIR/EBOOT.BIN"));
  CHECK(!io_state::parse_raw_disc_view("disc0:/sce_lbn0xz_size0x10"));
  CHECK(!io_state::parse_raw_disc_view("disc0:/sce_lbn0x10_size0xz"));
  CHECK(io_state::readable_size(*raw_view, 0x8000U, 0x1000U) == 0x1000U);
  CHECK(io_state::readable_size(*raw_view, 0x47000U, 0x2000U) == 0x1000U);
  CHECK(io_state::readable_size(*raw_view, 0x48000U, 0x2000U) == 0U);
  CHECK(io_state::add_signed(10U, -4).value_or(0U) == 6U);
  CHECK(!io_state::add_signed(3U, -4));

  ctrl_state::reset_for_tests();
  CHECK(ctrl_state::set_sampling_cycle(0U) == 0U);
  CHECK(ctrl_state::set_sampling_cycle(5555U) == 0U);
  CHECK(ctrl_state::set_sampling_cycle(20000U) == 5555U);
  CHECK(ctrl_state::set_sampling_cycle(5554U) == ctrl_state::invalid_value);
  CHECK(ctrl_state::set_sampling_cycle(20001U) == ctrl_state::invalid_value);
  CHECK(ctrl_state::set_sampling_mode(1U) == 0U);
  CHECK(ctrl_state::set_sampling_mode(0U) == 1U);
  CHECK(ctrl_state::set_sampling_mode(2U) == ctrl_state::invalid_mode);
  CHECK(ctrl_state::set_idle_cancel_threshold(-1, 128) == 0U);
  CHECK(ctrl_state::set_idle_cancel_threshold(-2, 0) ==
        ctrl_state::invalid_value);
  CHECK(ctrl_state::set_idle_cancel_threshold(0, 129) ==
        ctrl_state::invalid_value);

  CHECK(!refract::host::texture_color_doubling_enabled(0x00000100U));
  CHECK(refract::host::texture_color_doubling_enabled(0x00010000U));
  CHECK(refract::host::texture_color_doubling_enabled(0x00010104U));

  const auto sfo = refract::utility::make_savedata_sfo(
      "Example Game", "Slot 1", "Progress at the first checkpoint");
  CHECK(refract::utility::sfo_string(sfo, "TITLE") == "Example Game");
  CHECK(refract::utility::sfo_string(sfo, "SAVEDATA_TITLE") == "Slot 1");
  CHECK(refract::utility::sfo_string(sfo, "SAVEDATA_DETAIL") ==
        "Progress at the first checkpoint");
  CHECK(refract::utility::sfo_string(sfo, "MISSING").empty());

  const std::string oversized(2048U, 'x');
  const auto bounded =
      refract::utility::make_savedata_sfo(oversized, oversized, oversized);
  CHECK(refract::utility::sfo_string(bounded, "TITLE").size() == 127U);
  CHECK(refract::utility::sfo_string(bounded, "SAVEDATA_DETAIL").size() ==
        1023U);

  CHECK(refract::utility::sfo_string({}, "TITLE").empty());
  auto corrupt = sfo;
  corrupt.resize(24U);
  CHECK(refract::utility::sfo_string(corrupt, "TITLE").empty());
}
