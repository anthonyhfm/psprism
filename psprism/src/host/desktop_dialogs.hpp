#pragma once

#include "host.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace psprism::desktop {

struct DialogFrame {
  std::vector<std::uint8_t> pixels;
  std::uint32_t width{};
  std::uint32_t height{};
};

class DialogFrontend {
 public:
  DialogFrontend();
  ~DialogFrontend();
  DialogFrontend(const DialogFrontend&) = delete;
  DialogFrontend& operator=(const DialogFrontend&) = delete;

  void present(host::DialogModel model);
  void dismiss(std::uint64_t id);
  bool visible() const;
  std::uint64_t id() const;

  void handle_buttons(std::uint32_t buttons);
  void accept();
  void cancel();
  void handle_text(std::u16string_view text);
  void handle_backspace();
  std::optional<host::DialogResult> take_result(std::uint64_t id);
  DialogFrame rendered_frame() const;

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

int run_desktop_dialog_event_loop();
void request_desktop_dialog_event_loop_exit();
void process_desktop_dialog_events();

} // namespace psprism::desktop
