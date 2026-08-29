#include "host/desktop_dialogs.hpp"
#include "host/host.hpp"

#include <cstdint>
#include <vector>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) return __LINE__;                                         \
  } while (false)

namespace {

constexpr std::uint32_t psp_right = 0x000020U;
constexpr std::uint32_t psp_left = 0x000080U;
constexpr std::uint32_t psp_circle = 0x002000U;
constexpr std::uint32_t psp_cross = 0x004000U;
constexpr std::uint32_t psp_square = 0x008000U;

} // namespace

int main() {
  refract::desktop::DialogFrontend frontend;
  CHECK(!frontend.visible());
  CHECK(frontend.id() == 0U);
  CHECK(!frontend.take_result(1U).has_value());

  // Test 1: Savedata Load Dialog with Centered Carousel and Items
  {
    refract::host::DialogModel model;
    model.id = 101U;
    model.kind = refract::host::DialogKind::savedata_load;
    model.title = "Save Data";
    model.accept_label = "Load";
    model.cancel_label = "Back";
    model.confirm_with_cross = true;
    model.selected_item = 0U;

    refract::host::DialogItem item1;
    item1.title = "Monster Hunter Portable";
    item1.subtitle = "Hunter Lv. 5";
    item1.detail = "Progress: Quest 14 completed";
    item1.timestamp = "2026/08/25 22:00";
    item1.size = "1,420 KB";
    item1.empty = false;

    refract::host::DialogItem item2;
    item2.title = "Empty";
    item2.empty = true;

    model.items.push_back(item1);
    model.items.push_back(item2);

    frontend.present(model);
    CHECK(frontend.visible());
    CHECK(frontend.id() == 101U);

    auto frame = frontend.rendered_frame(1.0, 960, 544);
    CHECK(frame.width == 960U);
    CHECK(frame.height == 544U);
    CHECK(!frame.pixels.empty());

    // Navigate right to slot 1 (empty slot)
    frontend.handle_buttons(psp_right);
    auto frame2 = frontend.rendered_frame(1.0, 960, 544);
    CHECK(frame2.width == 960U);

    // Accept selection
    frontend.handle_buttons(psp_cross);
    CHECK(!frontend.visible());

    auto result = frontend.take_result(101U);
    CHECK(result.has_value());
    CHECK(result->id == 101U);
    CHECK(!result->cancelled);
    CHECK(result->selected_item == 1U);
  }

  // Test 2: Message Dialog (Yes / No) with Centered Text and Highlight
  {
    refract::host::DialogModel model;
    model.id = 102U;
    model.kind = refract::host::DialogKind::message;
    model.title = "Confirmation";
    model.message = "Do you want to overwrite the existing save data?";
    model.detail = "This operation cannot be undone.";
    model.yes_no = true;
    model.default_no = true;
    model.confirm_with_cross = true;

    frontend.present(model);
    CHECK(frontend.visible());
    CHECK(frontend.id() == 102U);

    auto frame = frontend.rendered_frame(1.0, 640, 460);
    CHECK(frame.width == 640U);
    CHECK(frame.height == 460U);
    CHECK(!frame.pixels.empty());

    // Switch from No to Yes using Dpad Left
    frontend.handle_buttons(psp_left);
    frontend.handle_buttons(psp_cross);

    auto result = frontend.take_result(102U);
    CHECK(result.has_value());
    CHECK(result->affirmative == true);
    CHECK(!result->cancelled);
  }

  // Test 3: Error Message Dialog
  {
    refract::host::DialogModel model;
    model.id = 103U;
    model.kind = refract::host::DialogKind::message;
    model.title = "System";
    model.message = "Error 0x80020148";
    model.accept_label = "OK";
    model.confirm_with_cross = true;

    frontend.present(model);
    CHECK(frontend.visible());

    auto frame = frontend.rendered_frame(1.0, 640, 460);
    CHECK(frame.width == 640U);

    frontend.accept();
    auto result = frontend.take_result(103U);
    CHECK(result.has_value());
    CHECK(!result->cancelled);
  }

  // Test 4: On-Screen Keyboard (OSK) Dialog with Square backspace and cancel
  {
    refract::host::DialogModel model;
    model.id = 104U;
    model.kind = refract::host::DialogKind::osk;
    model.title = "Keyboard";
    model.confirm_with_cross = true;

    refract::host::OskField field;
    field.label = "Player Name";
    field.text = u"Hero";
    field.limit = 12U;
    model.fields.push_back(field);

    frontend.present(model);
    CHECK(frontend.visible());

    // Use square for backspace
    frontend.handle_buttons(psp_square);

    // Type text via direct input
    frontend.handle_text(u"X");

    auto frame = frontend.rendered_frame(1.0, 800, 540);
    CHECK(frame.width == 800U);

    frontend.handle_buttons(psp_circle); // cancel
    auto result = frontend.take_result(104U);
    CHECK(result.has_value());
    CHECK(result->cancelled);
  }

  return 0;
}
