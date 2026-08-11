#include "utility_data.hpp"
#include "host/host.hpp"
#include "stubs/ctrl/ctrl_state.hpp"
#include "stubs/io/devctl_state.hpp"
#include "stubs/io/io_state.hpp"
#include "stubs/kernel/mailbox_state.hpp"
#include "stubs/kernel/memory_state.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) return __LINE__;                                         \
  } while (false)

int main() {
  struct FreeBlock {
    std::uint32_t address;
    std::uint32_t size;
  };
  const std::vector<FreeBlock> free_blocks{{0x08810000U, 0x2000U},
                                           {0x08900000U, 0x5000U}};
  CHECK(memory_state::maximum_free_size(0x100000U, 0x140000U,
                                        free_blocks) == 0x40000U);
  CHECK(memory_state::total_free_size(0x100000U, 0x140000U,
                                      free_blocks) == 0x47000U);
  CHECK(memory_state::maximum_free_size(0x180000U, 0x140000U,
                                        free_blocks) == 0x5000U);
  CHECK(memory_state::total_free_size(0x180000U, 0x140000U,
                                      free_blocks) == 0x7000U);

  const auto raw_view = io_state::parse_raw_disc_view(
      "disc0:/sce_lbn0x10_size0x40000");
  CHECK(raw_view.has_value());
  CHECK(raw_view->base == 0x8000U);
  CHECK(raw_view->size == 0x40000U);
  CHECK(io_state::parse_raw_disc_view(
            "umd0:/sce_lbn0x20_size0x1234") ==
        std::optional<io_state::FileView>({0x10000U, 0x1234U}));
  CHECK(!io_state::parse_raw_disc_view("disc0:/PSP_GAME/SYSDIR/EBOOT.BIN"));
  CHECK(io_state::is_whole_disc_path("umd0:"));
  CHECK(io_state::is_whole_disc_path("umd0:/"));
  CHECK(io_state::is_whole_disc_path("UMD0:/"));
  CHECK(!io_state::is_whole_disc_path("umd0:/PSP_GAME/USRDIR/data.bin"));
  CHECK(!io_state::is_whole_disc_path("disc0:"));
  CHECK(io_state::is_raw_disc_path("DISC0:/SCE_LBN0x10_SIZE0x20"));
  CHECK((io_state::parse_raw_disc_view("DISC0:/SCE_LBN0x10_SIZE0x20") ==
         io_state::FileView{0x8000U, 0x20U}));
  CHECK(io_state::error_from_errno(ENOENT) == 0x80010002U);
  CHECK(io_state::error_from_errno(EIO) == 0x80010005U);
  CHECK(io_state::error_from_errno(0) == io_state::generic_io_error);
  constexpr auto memory_stick = devctl_state::memory_stick_capacity();
  CHECK(memory_stick.maximum_clusters != 0U);
  CHECK(memory_stick.free_clusters == memory_stick.maximum_clusters);
  CHECK(memory_stick.maximum_sectors == memory_stick.maximum_clusters);
  CHECK(memory_stick.sector_size == 512U);
  CHECK(memory_stick.sectors_per_cluster == 64U);
  std::deque<mailbox_state::Message> fifo_mailbox;
  CHECK(mailbox_state::enqueue(fifo_mailbox, 0U, 0x08801000U, 7U));
  CHECK(mailbox_state::enqueue(fifo_mailbox, 0U, 0x08802000U, 1U));
  CHECK(!mailbox_state::enqueue(fifo_mailbox, 0U, 0x08801000U, 4U));
  CHECK(fifo_mailbox.front().address == 0x08801000U);
  std::deque<mailbox_state::Message> priority_mailbox;
  CHECK(mailbox_state::enqueue(priority_mailbox,
                               mailbox_state::message_priority_attribute,
                               0x08801000U, 7U));
  CHECK(mailbox_state::enqueue(priority_mailbox,
                               mailbox_state::message_priority_attribute,
                               0x08802000U, 1U));
  CHECK(priority_mailbox.front().address == 0x08802000U);
  CHECK(io_state::sector_byte_offset(7U).value_or(0U) == 14336U);
  CHECK(io_state::sector_byte_count(3U).value_or(0U) == 6144U);
  CHECK(io_state::complete_sector_count(6143U) == 2U);
  CHECK(io_state::signed_from_words(143536U, 0U) == 143536);
  CHECK(io_state::signed_from_words(0xffffffffU, 0xffffffffU) == -1);
  CHECK(!io_state::sector_byte_offset(
      std::numeric_limits<std::uint64_t>::max()));
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
