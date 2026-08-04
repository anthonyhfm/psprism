// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
//
// FTXUI Amalgamated Version 7.0.2 (59e8fbb)
//
// Usage (Split version):
// 1. Copy ftxui.hpp and ftxui.cpp into your project.
// 2. Add ftxui.cpp to your source files.
// 3. Ensure you are using C++17 and link against threads (-lpthread).
// 4. On Windows/MSVC, use /utf-8 and define UNICODE / _UNICODE and NOMINMAX.

#ifndef FTXUI_AMALGAMATED_HPP
#define FTXUI_AMALGAMATED_HPP

// On Windows, FTXUI requires UNICODE and NOMINMAX to be defined.
#if defined(_WIN32)
  #if !defined(UNICODE)
    #error "FTXUI requires UNICODE to be defined on Windows. Please add /DUNICODE /D_UNICODE to your compiler flags."
  #endif
  #if !defined(NOMINMAX)
    #error "FTXUI requires NOMINMAX to be defined on Windows. Please add /DNOMINMAX to your compiler flags."
  #endif
#endif

// Marker for detection
#define FTXUI_AMALGAMATED
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <stack>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#if defined(__sun) || defined(__illumos__)
#include <sys/termios.h>
#endif

// In an amalgamated build, we don't want any export/import annotations.
#if defined(FTXUI_AMALGAMATED)
#define FTXUI_EXPORT(component)
#define INSIDE_FTXUI_COMPONENT_IMPL(component) 0
#else

// Used to annotate symbols which are exported by the component named
// |component|. Note that this only does the right thing if the corresponding
// component target's sources are compiled with |IS_FTXUI_$component_IMPL|
// defined as 1. For example:
//
//   class FTXUI_EXPORT(FOO) Bar {};
//
// If IS_FTXUI_FOO_IMPL=1 at compile time, then Bar will be annotated using the
// FTXUI_EXPORT_ANNOTATION macro defined below. Otherwise it will be
// annotated using the FTXUI_IMPORT_ANNOTATION macro.
#define FTXUI_EXPORT(component)                         \
  FTXUI_MACRO_CONDITIONAL_(IS_FTXUI_##component##_IMPL, \
                           FTXUI_EXPORT_ANNOTATION, FTXUI_IMPORT_ANNOTATION)

// Indicates whether the current compilation unit is being compiled as part of
// the implementation of the component named |component|. Expands to |1| if
// |IS_FTXUI_$component_IMPL| is defined as |1|; expands to |0| otherwise.
//
// Note in particular that if |IS_FTXUI_$component_IMPL| is not defined at all,
// it is still fine to test INSIDE_FTXUI_COMPONENT_IMPL(component), which
// expands to |0| as expected.
#define INSIDE_FTXUI_COMPONENT_IMPL(component) \
  FTXUI_MACRO_CONDITIONAL_(IS_FTXUI_##component##_IMPL, 1, 0)

#endif

// Compiler-specific macros to annotate for export or import of a symbol. No-op
// in non-component builds. These should not see much if any direct use.
// Instead use the FTXUI_EXPORT macro defined above.
#if defined(COMPONENT_BUILD)
#if defined(WIN32)
#define FTXUI_EXPORT_ANNOTATION __declspec(dllexport)
#define FTXUI_IMPORT_ANNOTATION __declspec(dllimport)
#else  // defined(WIN32)
#define FTXUI_EXPORT_ANNOTATION __attribute__((visibility("default")))
#define FTXUI_IMPORT_ANNOTATION __attribute__((visibility("default")))
#endif  // defined(WIN32)
#else   // defined(COMPONENT_BUILD)
#define FTXUI_EXPORT_ANNOTATION
#define FTXUI_IMPORT_ANNOTATION
#endif  // defined(COMPONENT_BUILD)

// Below this point are several internal utility macros used for the
// implementation of the above macros. Not intended for external use.

#define FTXUI_MACRO_EXPAND(x) x

// Helper for conditional expansion to one of two token strings. If |condition|
// expands to |1| then this macro expands to |consequent|; otherwise it expands
// to |alternate|.
#define FTXUI_MACRO_CONDITIONAL_(condition, consequent, alternate) \
  FTXUI_MACRO_EXPAND(FTXUI_MACRO_SELECT_THIRD_ARGUMENT_(           \
      FTXUI_MACRO_CONDITIONAL_COMMA_(condition), consequent, alternate))

// Expands to a comma (,) iff its first argument expands to |1|. Used in
// conjunction with |FTXUI_MACRO_SELECT_THIRD_ARGUMENT_()|, as the presence
// or absense of an extra comma can be used to conditionally shift subsequent
// argument positions and thus influence which argument is selected.
#define FTXUI_MACRO_CONDITIONAL_COMMA_(...) \
  FTXUI_MACRO_EXPAND(FTXUI_MACRO_CONDITIONAL_COMMA_IMPL_(__VA_ARGS__, dummy))
#define FTXUI_MACRO_CONDITIONAL_COMMA_IMPL_(x, ...) \
  FTXUI_MACRO_CONDITIONAL_COMMA_##x##_
#define FTXUI_MACRO_CONDITIONAL_COMMA_1_ ,

// Helper which simply selects its third argument. Used in conjunction with
// |FTXUI_MACRO_CONDITIONAL_COMMA_()| above to implement conditional macro
// expansion.
#define FTXUI_MACRO_SELECT_THIRD_ARGUMENT_(...) \
  FTXUI_MACRO_EXPAND(                           \
      FTXUI_MACRO_SELECT_THIRD_ARGUMENT_IMPL_(__VA_ARGS__, dummy))
#define FTXUI_MACRO_SELECT_THIRD_ARGUMENT_IMPL_(a, b, c, ...) c


namespace ftxui::animation {
/// @brief RequestAnimationFrame is a function that requests a new frame to be
/// drawn in the next animation cycle.
///
/// @note This function is typically called by components that need to
/// update their state or appearance over time, such as animations or
/// transitions. This is useful when the change doesn't depend depend on the
/// events seen by the terminal, but rather on the passage of time.
///
/// Components who haven't completed their animation can call this function to
/// request a new frame to be drawn later.
///
/// When there is no new events and no animations to complete, no new frame is
/// drawn.
///
/// @ingroup component
FTXUI_EXPORT(COMPONENT) void RequestAnimationFrame();

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Duration = std::chrono::duration<float>;

// Parameter of Component::OnAnimation(param).
class FTXUI_EXPORT(COMPONENT) Params {
 public:
  explicit Params(Duration duration) : duration_(duration) {}

  /// The duration this animation step represents.
  Duration duration() const { return duration_; }

 private:
  Duration duration_;
};

namespace easing {
using Function = std::function<float(float)>;
// Linear interpolation (no easing)
FTXUI_EXPORT(COMPONENT) float Linear(float p);

// Quadratic easing; p^2
FTXUI_EXPORT(COMPONENT) float QuadraticIn(float p);
FTXUI_EXPORT(COMPONENT) float QuadraticOut(float p);
FTXUI_EXPORT(COMPONENT) float QuadraticInOut(float p);

// Cubic easing; p^3
FTXUI_EXPORT(COMPONENT) float CubicIn(float p);
FTXUI_EXPORT(COMPONENT) float CubicOut(float p);
FTXUI_EXPORT(COMPONENT) float CubicInOut(float p);

// Quartic easing; p^4
FTXUI_EXPORT(COMPONENT) float QuarticIn(float p);
FTXUI_EXPORT(COMPONENT) float QuarticOut(float p);
FTXUI_EXPORT(COMPONENT) float QuarticInOut(float p);

// Quintic easing; p^5
FTXUI_EXPORT(COMPONENT) float QuinticIn(float p);
FTXUI_EXPORT(COMPONENT) float QuinticOut(float p);
FTXUI_EXPORT(COMPONENT) float QuinticInOut(float p);

// Sine wave easing; sin(p * PI/2)
FTXUI_EXPORT(COMPONENT) float SineIn(float p);
FTXUI_EXPORT(COMPONENT) float SineOut(float p);
FTXUI_EXPORT(COMPONENT) float SineInOut(float p);

// Circular easing; sqrt(1 - p^2)
FTXUI_EXPORT(COMPONENT) float CircularIn(float p);
FTXUI_EXPORT(COMPONENT) float CircularOut(float p);
FTXUI_EXPORT(COMPONENT) float CircularInOut(float p);

// Exponential easing, base 2
FTXUI_EXPORT(COMPONENT) float ExponentialIn(float p);
FTXUI_EXPORT(COMPONENT) float ExponentialOut(float p);
FTXUI_EXPORT(COMPONENT) float ExponentialInOut(float p);

// Exponentially-damped sine wave easing
FTXUI_EXPORT(COMPONENT) float ElasticIn(float p);
FTXUI_EXPORT(COMPONENT) float ElasticOut(float p);
FTXUI_EXPORT(COMPONENT) float ElasticInOut(float p);

// Overshooting cubic easing;
FTXUI_EXPORT(COMPONENT) float BackIn(float p);
FTXUI_EXPORT(COMPONENT) float BackOut(float p);
FTXUI_EXPORT(COMPONENT) float BackInOut(float p);

// Exponentially-decaying bounce easing
FTXUI_EXPORT(COMPONENT) float BounceIn(float p);
FTXUI_EXPORT(COMPONENT) float BounceOut(float p);
FTXUI_EXPORT(COMPONENT) float BounceInOut(float p);
}  // namespace easing

class FTXUI_EXPORT(COMPONENT) Animator {
 public:
  explicit Animator(float* from,
                    float to = 0.f,
                    Duration duration = std::chrono::milliseconds(250),
                    easing::Function easing_function = easing::Linear,
                    Duration delay = std::chrono::milliseconds(0));

  void OnAnimation(Params&);

  float to() const { return to_; }

 private:
  float* value_;
  float from_;
  float to_;
  Duration duration_;
  easing::Function easing_function_;
  Duration current_;
};

}  // namespace ftxui::animation






namespace ftxui {

class FTXUI_EXPORT(COMPONENT) CapturedMouseInterface {
 public:
  CapturedMouseInterface() = default;
  CapturedMouseInterface(const CapturedMouseInterface&) = default;
  CapturedMouseInterface(CapturedMouseInterface&&) = delete;
  CapturedMouseInterface& operator=(const CapturedMouseInterface&) = default;
  CapturedMouseInterface& operator=(CapturedMouseInterface&&) = delete;
  virtual ~CapturedMouseInterface() = default;
};
using CapturedMouse = std::unique_ptr<CapturedMouseInterface>;
}  // namespace ftxui






namespace ftxui {

/// @brief A mouse event. It contains the coordinate of the mouse, the button
/// pressed and the modifier (shift, ctrl, meta).
/// @ingroup component
struct FTXUI_EXPORT(COMPONENT) Mouse {
  enum Button : uint8_t {
    Left = 0,
    Middle = 1,
    Right = 2,
    None = 3,
    WheelUp = 4,
    WheelDown = 5,
    WheelLeft = 6,   /// Supported terminal only.
    WheelRight = 7,  /// Supported terminal only.
  };

  enum Motion : uint8_t {
    Released = 0,
    Pressed = 1,
    Moved = 2,
  };

  // Button
  Button button = Button::None;

  // Motion
  Motion motion = Motion::Pressed;

  // Modifiers:
  bool shift = false;
  bool meta = false;
  bool control = false;

  // Coordinates:
  int x = 0;
  int y = 0;
};

}  // namespace ftxui



namespace ftxui {

class App;
class ComponentBase;

/// @brief Represent an event. It can be key press event, a terminal resize, or
/// more ...
///
/// For example:
/// - Printable character can be created using Event::Character('a').
/// - Some special are predefined, like Event::ArrowLeft.
/// - One can find arbitrary code for special Events using:
///   ./example/util/print_key_press
///  For instance, CTLR+A maps to Event::Special({1});
///
/// Useful documentation about xterm specification:
/// https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
///
/// @ingroup component
struct FTXUI_EXPORT(COMPONENT) Event {
  // --- Constructor section ---------------------------------------------------
  static Event Character(std::string_view);
  static Event Character(char);
  static Event Character(wchar_t);
  static Event Special(std::string_view);
  static Event Special(std::initializer_list<char>);
  static Event Mouse(std::string_view, Mouse mouse);
  static Event CursorPosition(std::string_view, int x, int y);  // Internal
  static Event CursorShape(std::string_view, int shape);        // Internal
  static Event TerminalNameVersion(std::string_view,
                                   std::string name,
                                   int version);
  static Event TerminalEmulator(std::string_view,
                                std::string name,
                                std::string version);
  static Event TerminalCapabilities(std::string_view,
                                    std::vector<int> capabilities);

  // --- Arrow ---
  static const Event ArrowLeft;
  static const Event ArrowRight;
  static const Event ArrowUp;
  static const Event ArrowDown;

  static const Event ArrowLeftCtrl;
  static const Event ArrowRightCtrl;
  static const Event ArrowUpCtrl;
  static const Event ArrowDownCtrl;

  // --- Other ---
  static const Event Backspace;
  static const Event Delete;
  static const Event Return;
  static const Event Escape;
  static const Event Tab;
  static const Event TabReverse;

  // --- Navigation keys ---
  static const Event Insert;
  static const Event Home;
  static const Event End;
  static const Event PageUp;
  static const Event PageDown;

  // --- Function keys ---
  // MSVC's dllexport/dllimport only applies correctly to the first
  // declarator in a multi-name declaration, so these must be declared one
  // per statement (see error C2487) when built as a Windows DLL.
  static const Event F1;
  static const Event F2;
  static const Event F3;
  static const Event F4;
  static const Event F5;
  static const Event F6;
  static const Event F7;
  static const Event F8;
  static const Event F9;
  static const Event F10;
  static const Event F11;
  static const Event F12;

  // --- Control keys ---
  static const Event a;
  static const Event A;
  static const Event CtrlA;
  static const Event AltA;
  static const Event CtrlAltA;
  static const Event b;
  static const Event B;
  static const Event CtrlB;
  static const Event AltB;
  static const Event CtrlAltB;
  static const Event c;
  static const Event C;
  static const Event CtrlC;
  static const Event AltC;
  static const Event CtrlAltC;
  static const Event d;
  static const Event D;
  static const Event CtrlD;
  static const Event AltD;
  static const Event CtrlAltD;
  static const Event e;
  static const Event E;
  static const Event CtrlE;
  static const Event AltE;
  static const Event CtrlAltE;
  static const Event f;
  static const Event F;
  static const Event CtrlF;
  static const Event AltF;
  static const Event CtrlAltF;
  static const Event g;
  static const Event G;
  static const Event CtrlG;
  static const Event AltG;
  static const Event CtrlAltG;
  static const Event h;
  static const Event H;
  static const Event CtrlH;
  static const Event AltH;
  static const Event CtrlAltH;
  static const Event i;
  static const Event I;
  static const Event CtrlI;
  static const Event AltI;
  static const Event CtrlAltI;
  static const Event j;
  static const Event J;
  static const Event CtrlJ;
  static const Event AltJ;
  static const Event CtrlAltJ;
  static const Event k;
  static const Event K;
  static const Event CtrlK;
  static const Event AltK;
  static const Event CtrlAltK;
  static const Event l;
  static const Event L;
  static const Event CtrlL;
  static const Event AltL;
  static const Event CtrlAltL;
  static const Event m;
  static const Event M;
  static const Event CtrlM;
  static const Event AltM;
  static const Event CtrlAltM;
  static const Event n;
  static const Event N;
  static const Event CtrlN;
  static const Event AltN;
  static const Event CtrlAltN;
  static const Event o;
  static const Event O;
  static const Event CtrlO;
  static const Event AltO;
  static const Event CtrlAltO;
  static const Event p;
  static const Event P;
  static const Event CtrlP;
  static const Event AltP;
  static const Event CtrlAltP;
  static const Event q;
  static const Event Q;
  static const Event CtrlQ;
  static const Event AltQ;
  static const Event CtrlAltQ;
  static const Event r;
  static const Event R;
  static const Event CtrlR;
  static const Event AltR;
  static const Event CtrlAltR;
  static const Event s;
  static const Event S;
  static const Event CtrlS;
  static const Event AltS;
  static const Event CtrlAltS;
  static const Event t;
  static const Event T;
  static const Event CtrlT;
  static const Event AltT;
  static const Event CtrlAltT;
  static const Event u;
  static const Event U;
  static const Event CtrlU;
  static const Event AltU;
  static const Event CtrlAltU;
  static const Event v;
  static const Event V;
  static const Event CtrlV;
  static const Event AltV;
  static const Event CtrlAltV;
  static const Event w;
  static const Event W;
  static const Event CtrlW;
  static const Event AltW;
  static const Event CtrlAltW;
  static const Event x;
  static const Event X;
  static const Event CtrlX;
  static const Event AltX;
  static const Event CtrlAltX;
  static const Event y;
  static const Event Y;
  static const Event CtrlY;
  static const Event AltY;
  static const Event CtrlAltY;
  static const Event z;
  static const Event Z;
  static const Event CtrlZ;
  static const Event AltZ;
  static const Event CtrlAltZ;

  // --- Custom ---
  static const Event Custom;

  //--- Method section ---------------------------------------------------------
  bool operator==(const Event& other) const { return input_ == other.input_; }
  bool operator!=(const Event& other) const { return !operator==(other); }
  bool operator<(const Event& other) const { return input_ < other.input_; }

  const std::string& input() const { return input_; }

  bool is_character() const { return type_ == Type::Character; }
  std::string character() const { return input_; }

  bool is_mouse() const { return type_ == Type::Mouse; }
  struct Mouse& mouse() { return data_.mouse; }

  // --- Internal Method section -----------------------------------------------
  bool is_cursor_position() const { return type_ == Type::CursorPosition; }
  int cursor_x() const { return data_.cursor.x; }
  int cursor_y() const { return data_.cursor.y; }

  bool is_cursor_shape() const { return type_ == Type::CursorShape; }
  int cursor_shape() const { return data_.cursor_shape; }

  bool IsTerminalNameVersion() const;
  const std::string& TerminalName() const;
  int TerminalVersion() const;

  bool IsTerminalEmulator() const;
  const std::string& TerminalEmulatorName() const;
  const std::string& TerminalEmulatorVersion() const;

  bool IsTerminalCapabilities() const;
  const std::vector<int>& TerminalCapabilities() const;
  std::vector<std::string> TerminalCapabilityNames() const;

  // Debug
  std::string DebugString() const;

  //--- State section ----------------------------------------------------------
  App* screen_ = nullptr;

 private:
  friend ComponentBase;
  friend App;
  enum class Type : uint8_t {
    Unknown,
    Character,
    Mouse,
    CursorPosition,
    CursorShape,
    TerminalNameVersion,
    TerminalEmulator,
    TerminalCapabilities,
  };
  Type type_ = Type::Unknown;
  struct Cursor {
    int x = 0;
    int y = 0;
  };

  union {
    struct Mouse mouse;
    struct Cursor cursor;
    int cursor_shape;
    int terminal_version;
  } data_ = {};

  std::string input_;
  std::shared_ptr<std::string> terminal_name_;
  std::shared_ptr<std::string> terminal_emulator_version_;
  std::shared_ptr<std::vector<int>> terminal_capabilities_;
};

}  // namespace ftxui


namespace ftxui {
class AnimationTask {};
using Closure = std::function<void()>;
using Task = std::variant<Event, Closure, AnimationTask>;
}  // namespace ftxui







namespace ftxui {

/// @brief Box is a structure that represents a rectangular area in a 2D space.
///
/// It is defined by its minimum and maximum coordinates along the x and y axes.
/// Note that the coordinates are inclusive, meaning that the box includes both
/// the minimum and maximum values.
///
/// @ingroup screen
struct FTXUI_EXPORT(SCREEN) Box {
  int x_min = 0;
  int x_max = 0;
  int y_min = 0;
  int y_max = 0;

  static auto Intersection(Box a, Box b) -> Box;
  static auto Union(Box a, Box b) -> Box;
  void Shift(int x, int y);
  bool Contain(int x, int y) const;
  bool IsEmpty() const;
  bool operator==(const Box& other) const;
  bool operator!=(const Box& other) const;
};

}  // namespace ftxui





#ifdef RGB
// Workaround for wingdi.h (via Windows.h) defining macros that break things.
// https://docs.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-rgb
#undef RGB
#endif

namespace ftxui {

/// @brief Color is a class that represents a color in the terminal user
/// interface.
///
/// @ingroup screen
class FTXUI_EXPORT(SCREEN) Color {
 public:
  enum Palette1 : uint8_t;
  enum Palette16 : uint8_t;
  enum Palette256 : uint8_t;

  // NOLINTBEGIN
  Color();                  // Transparent.
  Color(Palette1 index);    // Transparent.
  Color(Palette16 index);   // Implicit conversion from index to Color.
  Color(Palette256 index);  // Implicit conversion from index to Color.
  // NOLINTEND
  Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255);
  static Color RGB(uint8_t red, uint8_t green, uint8_t blue);
  static Color HSV(uint8_t hue, uint8_t saturation, uint8_t value);
  static Color RGBA(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
  static Color HSVA(uint8_t hue,
                    uint8_t saturation,
                    uint8_t value,
                    uint8_t alpha);
  static Color Interpolate(float t, const Color& a, const Color& b);
  static Color Blend(const Color& lhs, const Color& rhs);

  //---------------------------
  // List of colors:
  //---------------------------
  // clang-format off
  enum Palette1 : uint8_t{
    Default, // Transparent
  };

  enum Palette16 : uint8_t {
    Black        = 0,
    Red          = 1,
    Green        = 2,
    Yellow       = 3,
    Blue         = 4,
    Magenta      = 5,
    Cyan         = 6,
    GrayLight    = 7,
    GrayDark     = 8,
    RedLight     = 9,
    GreenLight   = 10,
    YellowLight  = 11,
    BlueLight    = 12,
    MagentaLight = 13,
    CyanLight    = 14,
    White        = 15,
  };

  enum Palette256 : uint8_t {
    Aquamarine1        = 122,
    Aquamarine1Bis     = 86,
    Aquamarine3        = 79,
    Blue1              = 21,
    Blue3              = 19,
    Blue3Bis           = 20,
    BlueViolet         = 57,
    CadetBlue          = 72,
    CadetBlueBis       = 73,
    Chartreuse1        = 118,
    Chartreuse2        = 112,
    Chartreuse2Bis     = 82,
    Chartreuse3        = 70,
    Chartreuse3Bis     = 76,
    Chartreuse4        = 64,
    CornflowerBlue     = 69,
    Cornsilk1          = 230,
    Cyan1              = 51,
    Cyan2              = 50,
    Cyan3              = 43,
    DarkBlue           = 18,
    DarkCyan           = 36,
    DarkGoldenrod      = 136,
    DarkGreen          = 22,
    DarkKhaki          = 143,
    DarkMagenta        = 90,
    DarkMagentaBis     = 91,
    DarkOliveGreen1    = 191,
    DarkOliveGreen1Bis = 192,
    DarkOliveGreen2    = 155,
    DarkOliveGreen3    = 107,
    DarkOliveGreen3Bis = 113,
    DarkOliveGreen3Ter = 149,
    DarkOrange         = 208,
    DarkOrange3        = 130,
    DarkOrange3Bis     = 166,
    DarkRed            = 52,
    DarkRedBis         = 88,
    DarkSeaGreen       = 108,
    DarkSeaGreen1      = 158,
    DarkSeaGreen1Bis   = 193,
    DarkSeaGreen2      = 151,
    DarkSeaGreen2Bis   = 157,
    DarkSeaGreen3      = 115,
    DarkSeaGreen3Bis   = 150,
    DarkSeaGreen4      = 65,
    DarkSeaGreen4Bis   = 71,
    DarkSlateGray1     = 123,
    DarkSlateGray2     = 87,
    DarkSlateGray3     = 116,
    DarkTurquoise      = 44,
    DarkViolet         = 128,
    DarkVioletBis      = 92,
    DeepPink1          = 198,
    DeepPink1Bis       = 199,
    DeepPink2          = 197,
    DeepPink3          = 161,
    DeepPink3Bis       = 162,
    DeepPink4          = 125,
    DeepPink4Bis          = 89,
    DeepPink4Ter       = 53,
    DeepSkyBlue1       = 39,
    DeepSkyBlue2       = 38,
    DeepSkyBlue3       = 31,
    DeepSkyBlue3Bis    = 32,
    DeepSkyBlue4       = 23,
    DeepSkyBlue4Bis    = 24,
    DeepSkyBlue4Ter    = 25,
    DodgerBlue1        = 33,
    DodgerBlue2        = 27,
    DodgerBlue3        = 26,
    Gold1              = 220,
    Gold3              = 142,
    Gold3Bis           = 178,
    Green1             = 46,
    Green3             = 34,
    Green3Bis          = 40,
    Green4             = 28,
    GreenYellow        = 154,
    Grey0              = 16,
    Grey100            = 231,
    Grey11             = 234,
    Grey15             = 235,
    Grey19             = 236,
    Grey23             = 237,
    Grey27             = 238,
    Grey3              = 232,
    Grey30             = 239,
    Grey35             = 240,
    Grey37             = 59,
    Grey39             = 241,
    Grey42             = 242,
    Grey46             = 243,
    Grey50             = 244,
    Grey53             = 102,
    Grey54             = 245,
    Grey58             = 246,
    Grey62             = 247,
    Grey63             = 139,
    Grey66             = 248,
    Grey69             = 145,
    Grey7              = 233,
    Grey70             = 249,
    Grey74             = 250,
    Grey78             = 251,
    Grey82             = 252,
    Grey84             = 188,
    Grey85             = 253,
    Grey89             = 254,
    Grey93             = 255,
    Honeydew2          = 194,
    HotPink            = 205,
    HotPink2           = 169,
    HotPink3           = 132,
    HotPink3Bis        = 168,
    HotPinkBis         = 206,
    IndianRed          = 131,
    IndianRed1         = 203,
    IndianRed1Bis      = 204,
    IndianRedBis       = 167,
    Khaki1             = 228,
    Khaki3             = 185,
    LightCoral         = 210,
    LightCyan1Bis      = 195,
    LightCyan3         = 152,
    LightGoldenrod1    = 227,
    LightGoldenrod2    = 186,
    LightGoldenrod2Bis = 221,
    LightGoldenrod2Ter = 222,
    LightGoldenrod3    = 179,
    LightGreen         = 119,
    LightGreenBis      = 120,
    LightPink1         = 217,
    LightPink3         = 174,
    LightPink4         = 95,
    LightSalmon1       = 216,
    LightSalmon3       = 137,
    LightSalmon3Bis       = 173,
    LightSeaGreen      = 37,
    LightSkyBlue1      = 153,
    LightSkyBlue3      = 109,
    LightSkyBlue3Bis      = 110,
    LightSlateBlue     = 105,
    LightSlateGrey     = 103,
    LightSteelBlue     = 147,
    LightSteelBlue1    = 189,
    LightSteelBlue3    = 146,
    LightYellow3       = 187,
    Magenta1           = 201,
    Magenta2           = 165,
    Magenta2Bis        = 200,
    Magenta3           = 127,
    Magenta3Bis        = 163,
    Magenta3Ter        = 164,
    MediumOrchid       = 134,
    MediumOrchid1      = 171,
    MediumOrchid1Bis   = 207,
    MediumOrchid3      = 133,
    MediumPurple       = 104,
    MediumPurple1      = 141,
    MediumPurple2      = 135,
    MediumPurple2Bis   = 140,
    MediumPurple3      = 97,
    MediumPurple3Bis   = 98,
    MediumPurple4      = 60,
    MediumSpringGreen  = 49,
    MediumTurquoise    = 80,
    MediumVioletRed    = 126,
    MistyRose1         = 224,
    MistyRose3         = 181,
    NavajoWhite1       = 223,
    NavajoWhite3       = 144,
    NavyBlue           = 17,
    Orange1            = 214,
    Orange3            = 172,
    Orange4            = 58,
    Orange4Bis         = 94,
    OrangeRed1         = 202,
    Orchid             = 170,
    Orchid1            = 213,
    Orchid2            = 212,
    PaleGreen1         = 121,
    PaleGreen1Bis      = 156,
    PaleGreen3         = 114,
    PaleGreen3Bis      = 77,
    PaleTurquoise1     = 159,
    PaleTurquoise4     = 66,
    PaleVioletRed1     = 211,
    Pink1              = 218,
    Pink3              = 175,
    Plum1              = 219,
    Plum2              = 183,
    Plum3              = 176,
    Plum4              = 96,
    Purple             = 129,
    Purple3            = 56,
    Purple4            = 54,
    Purple4Bis         = 55,
    PurpleBis          = 93,
    Red1               = 196,
    Red3               = 124,
    Red3Bis            = 160,
    RosyBrown          = 138,
    RoyalBlue1         = 63,
    Salmon1            = 209,
    SandyBrown         = 215,
    SeaGreen1          = 84,
    SeaGreen1Bis       = 85,
    SeaGreen2          = 83,
    SeaGreen3          = 78,
    SkyBlue1           = 117,
    SkyBlue2           = 111,
    SkyBlue3           = 74,
    SlateBlue1         = 99,
    SlateBlue3         = 61,
    SlateBlue3Bis      = 62,
    SpringGreen1       = 48,
    SpringGreen2       = 42,
    SpringGreen2Bis    = 47,
    SpringGreen3       = 35,
    SpringGreen3Bis    = 41,
    SpringGreen4       = 29,
    SteelBlue          = 67,
    SteelBlue1         = 75,
    SteelBlue1Bis      = 81,
    SteelBlue3         = 68,
    Tan                = 180,
    Thistle1           = 225,
    Thistle3           = 182,
    Turquoise2         = 45,
    Turquoise4         = 30,
    Violet             = 177,
    Wheat1             = 229,
    Wheat4             = 101,
    Yellow1            = 226,
    Yellow2            = 190,
    Yellow3            = 148,
    Yellow3Bis         = 184,
    Yellow4            = 100,
    Yellow4Bis         = 106,
  };
  // clang-format on

  // --- Operators ------
  bool operator==(const Color& rhs) const;
  bool operator!=(const Color& rhs) const;

  std::string Print(bool is_background_color) const;
  void PrintTo(std::string& out, bool is_background_color) const;
  bool IsOpaque() const { return alpha_ == 255; }

 private:
  enum class ColorType : uint8_t {
    Palette1,
    Palette16,
    Palette256,
    TrueColor,
  };
  ColorType type_ = ColorType::Palette1;
  uint8_t red_ = 0;
  uint8_t green_ = 0;
  uint8_t blue_ = 0;
  uint8_t alpha_ = 0;
};

inline namespace literals {

/// @brief Creates a color from a combined hex RGB representation,
/// e.g. 0x808000_rgb
FTXUI_EXPORT(SCREEN) Color operator""_rgb(unsigned long long int combined);

}  // namespace literals

}  // namespace ftxui


namespace ftxui {

/// @brief A Unicode character and its associated style.
///
/// @note This struct was previously named Pixel.
///
/// @ingroup screen
struct FTXUI_EXPORT(SCREEN) Cell {
  Cell()
      : blink(false),
        bold(false),
        dim(false),
        italic(false),
        inverted(false),
        underlined(false),
        underlined_double(false),
        strikethrough(false),
        automerge(false) {}

  // A bit field representing the style:
  bool blink : 1;
  bool bold : 1;
  bool dim : 1;
  bool italic : 1;
  bool inverted : 1;
  bool underlined : 1;
  bool underlined_double : 1;
  bool strikethrough : 1;
  bool automerge : 1;

  // The hyperlink associated with the cell.
  // 0 is the default value, meaning no hyperlink.
  // It's an index for accessing Screen meta data
  uint8_t hyperlink = 0;

  // The graphemes stored into the cell. To support combining characters,
  // like: a?, this can potentially contain multiple codepoints.
  std::string character = "";

  // Colors:
  Color background_color = Color::Default;
  Color foreground_color = Color::Default;
};

}  // namespace ftxui


namespace ftxui {

/// @brief A rectangular grid of Cell.
///
/// @note This class was previously named Image.
///
/// @ingroup screen
class FTXUI_EXPORT(SCREEN) Surface {
 public:
  // Constructors:
  Surface() = delete;
  Surface(int dimx, int dimy);

  // Destructor:
  virtual ~Surface() = default;

  // Copy:
  Surface(const Surface&) = default;
  Surface& operator=(const Surface&) = default;

  // Access a character in the grid at a given position.
  std::string& at(int x, int y);
  const std::string& at(int x, int y) const;

  // Access a cell (Cell) in the grid at a given position.
  Cell& CellAt(int x, int y);
  const Cell& CellAt(int x, int y) const;

  // [Deprecated] alias for CellAt.
  Cell& PixelAt(int x, int y) { return CellAt(x, y); }
  const Cell& PixelAt(int x, int y) const { return CellAt(x, y); }

  // Get screen dimensions.
  int dimx() const { return dimx_; }
  int dimy() const { return dimy_; }

  // Fill the surface with space and default style
  void Clear();

  // ABI Reserve:
  virtual void Reserved1();
  virtual void Reserved2();
  virtual void Reserved3();
  virtual void Reserved4();
  virtual void Reserved5();
  virtual void Reserved6();
  virtual void Reserved7();
  virtual void Reserved8();

  Box stencil;

 protected:
  Cell& FastCellAt(int x, int y);
  const Cell& FastCellAt(int x, int y) const;

  int dimx_;
  int dimy_;
  std::vector<Cell> cells_;
};

}  // namespace ftxui





namespace ftxui {

/// @brief Dimensions is a structure that represents the size of the terminal
/// @ingroup screen
struct FTXUI_EXPORT(SCREEN) Dimensions {
  int dimx;
  int dimy;
};

namespace Terminal {
FTXUI_EXPORT(SCREEN) Dimensions Size();
FTXUI_EXPORT(SCREEN) void SetFallbackSize(const Dimensions& fallbackSize);

/// @brief Color is an enumeration that represents the color support of the
/// terminal.
/// @ingroup screen
enum Color : uint8_t {
  Palette1,
  Palette16,
  Palette256,
  TrueColor,
};
FTXUI_EXPORT(SCREEN) Color ColorSupport();
FTXUI_EXPORT(SCREEN) void SetColorSupport(Color color);

/// @brief Quirks is a structure that represents various terminal-specific
/// behaviors that may require fallbacks.
/// @ingroup screen
class FTXUI_EXPORT(SCREEN) Quirks {
 public:
  Quirks();
  ~Quirks();
  Quirks(const Quirks&);
  Quirks& operator=(const Quirks&);
  Quirks(Quirks&&) noexcept;
  Quirks& operator=(Quirks&&) noexcept;

  /// @brief Whether the terminal font supports the 8 Unicode block characters.
  bool BlockCharacters() const;
  void SetBlockCharacters(bool v);

  /// @brief Whether the terminal correctly handles hiding the cursor.
  bool CursorHiding() const;
  void SetCursorHiding(bool v);

  /// @brief Whether the terminal should use ASCII characters for components.
  bool ComponentAscii() const;
  void SetComponentAscii(bool v);

  /// @brief The level of color support of the terminal.
  Color ColorSupport() const;
  void SetColorSupport(Color v);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
FTXUI_EXPORT(SCREEN) Quirks GetQuirks();
FTXUI_EXPORT(SCREEN) void SetQuirks(const Quirks& quirks);

/// @brief TerminalInfo is a structure that contains information about the
/// terminal.
/// @ingroup screen
class FTXUI_EXPORT(SCREEN) TerminalInfo {
 public:
  TerminalInfo();
  ~TerminalInfo();
  TerminalInfo(const TerminalInfo&) = delete;
  TerminalInfo& operator=(const TerminalInfo&) = delete;
  TerminalInfo(TerminalInfo&&) noexcept;
  TerminalInfo& operator=(TerminalInfo&&) noexcept;

  void SetTerm(std::string_view term);
  void SetColorterm(std::string_view colorterm);
  void SetTermProgram(std::string_view term_program);
  void SetTerminalName(std::string_view terminal_name);
  void SetTerminalEmulatorName(std::string_view terminal_emulator_name);
  void SetCapabilities(std::vector<int> capabilities);

  Color ComputeColorSupport() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// @brief Compute the color support based on environment variables and terminal
/// identification.
/// @param term The TERM environment variable.
/// @param colorterm The COLORTERM environment variable.
/// @param term_program The TERM_PROGRAM environment variable.
/// @param terminal_name The terminal name (from DA2).
/// @param terminal_emulator_name The terminal emulator name (from XTVERSION).
/// @param capabilities The terminal capabilities (from DA1).
FTXUI_EXPORT(SCREEN)
Color ComputeColorSupport(std::string_view term,
                          std::string_view colorterm,
                          std::string_view term_program,
                          std::string_view terminal_name,
                          std::string_view terminal_emulator_name,
                          const std::vector<int>& capabilities);

}  // namespace Terminal

}  // namespace ftxui


namespace ftxui {

/// @brief Define how the Screen's dimensions should look like.
/// @ingroup screen
namespace Dimension {
FTXUI_EXPORT(SCREEN) Dimensions Fixed(int);
FTXUI_EXPORT(SCREEN) Dimensions Full();
}  // namespace Dimension

/// @brief A rectangular grid of Cell.
/// @ingroup screen
class FTXUI_EXPORT(SCREEN) Screen : public Surface {
 public:
  // Constructors:
  Screen(int dimx, int dimy);
  static Screen Create(Dimensions dimension);
  static Screen Create(Dimensions width, Dimensions height);

  // Destructor:
  ~Screen() override = default;

  // Copy:
  Screen(const Screen&) = default;
  Screen& operator=(const Screen&) = default;

  std::string ToString() const;
  void ToString(std::string& ss) const;

  // Print the Screen on to the terminal.
  void Print() const;

  // Fill the screen with space and reset any screen state, like hyperlinks, and
  // cursor
  void Clear();

  // Move the terminal cursor n-lines up with n = dimy().
  std::string ResetPosition(bool clear = false) const;
  void ResetPosition(std::string& ss, bool clear = false) const;

  void ApplyShader();

  struct Cursor {
    int x = 0;
    int y = 0;

    enum Shape : uint8_t {
      Hidden = 0,
      BlockBlinking = 1,
      Block = 2,
      UnderlineBlinking = 3,
      Underline = 4,
      BarBlinking = 5,
      Bar = 6,
    };
    Shape shape = Hidden;
  };

  Cursor cursor() const { return cursor_; }
  void SetCursor(Cursor cursor) { cursor_ = cursor; }

  // ABI Reserve:
  void Reserved1() override;
  void Reserved2() override;
  void Reserved3() override;
  void Reserved4() override;
  void Reserved5() override;
  void Reserved6() override;
  void Reserved7() override;
  void Reserved8() override;

  // Store an hyperlink in the screen. Return the id of the hyperlink. The id is
  // used to identify the hyperlink when the user click on it.
  uint8_t RegisterHyperlink(std::string_view link);
  const std::string& Hyperlink(uint8_t id) const;

  using SelectionStyle = std::function<void(Cell&)>;
  const SelectionStyle& GetSelectionStyle() const;
  void SetSelectionStyle(SelectionStyle decorator);

 protected:
  Cursor cursor_;
  std::vector<std::string> hyperlinks_ = {""};

  // The current selection style. This is overridden by various dom elements.
  SelectionStyle selection_style_ = [](Cell& cell) { cell.inverted ^= true; };
};

}  // namespace ftxui


namespace ftxui {
class ComponentBase;
using Component = std::shared_ptr<ComponentBase>;
struct Event;
class Selection;
class TaskRunner;

/// @brief App is a class that manages the application lifecycle.
/// It is responsible for initializing the terminal, running the main loop,
/// and cleaning up on exit.
///
/// @note This class was previously named ScreenInteractive.
///
/// @ingroup component
class FTXUI_EXPORT(COMPONENT) App : public Screen {
 public:
  // Constructors:

  /// @brief Create an App with a fixed size.
  /// @param dimx The width of the app.
  /// @param dimy The height of the app.
  static App FixedSize(int dimx, int dimy);

  /// @brief Create an App taking the full terminal size. This is using the
  /// alternate screen buffer to avoid messing with the terminal content.
  /// @note This is the same as `App::FullscreenAlternateScreen()`
  static App Fullscreen();

  /// @brief Create an App taking the full terminal size. The primary screen
  /// buffer is being used. It means if the terminal is resized, the previous
  /// content might mess up with the terminal content.
  static App FullscreenPrimaryScreen();

  /// @brief Create an App taking the full terminal size. This is using the
  /// alternate screen buffer to avoid messing with the terminal content.
  static App FullscreenAlternateScreen();

  /// @brief Create an App whose width and height match the component being
  /// drawn.
  static App FitComponent();

  /// @brief Create an App whose width match the terminal output width and
  /// the height matches the component being drawn.
  static App TerminalOutput();

  // Destructor.
  ~App() override;

  App(App&&) noexcept;
  App& operator=(App&&) noexcept;
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // Options. Must be called before Loop().

  /// @brief Set whether mouse is tracked and events reported.
  /// @param enable Whether to enable mouse event tracking.
  /// @note Mouse tracking is enabled by default.
  /// @note Mouse tracking is only supported on terminals that supports it.
  /// @note This must be called before calling `App::Loop`.
  void TrackMouse(bool enable = true);

  /// @brief Enable or disable automatic piped input handling.
  /// When enabled, FTXUI will detect piped input and redirect stdin from
  /// /dev/tty for keyboard input, allowing applications to read piped data
  /// while still receiving interactive keyboard events.
  /// @param enable Whether to enable piped input handling. Default is true.
  /// @note This must be called before Loop().
  /// @note This feature is enabled by default.
  /// @note This feature is only available on POSIX systems (Linux/macOS).
  void HandlePipedInput(bool enable = true);

  /// @brief Return the currently active app, nullptr if none.
  static App* Active();

  // Start/Stop the main loop.

  /// @brief Execute the main loop.
  /// @param component The component to draw.
  void Loop(Component component);

  /// @brief Exit the main loop.
  void Exit();

  /// @brief Return a function to exit the main loop.
  Closure ExitLoopClosure();

  /// @brief Decorate a function. The outputted one will execute similarly to
  /// the inputted one, but with the currently active app terminal hooks
  /// temporarily uninstalled.
  Closure WithRestoredIO(Closure fn);

  /// @brief FTXUI implements handlers for Ctrl-C and Ctrl-Z. By default, these
  /// handlers are executed, even if the component catches the event. This avoid
  /// users handling every event to be trapped in the application. However, in
  /// some cases, the application may want to handle these events itself. In
  /// this case, the application can force FTXUI to not handle these events by
  /// calling the following functions with force=true.
  void ForceHandleCtrlC(bool force = true);

  /// @brief Force FTXUI to handle or not handle Ctrl-Z, even if the component
  /// catches the Event::CtrlZ.
  void ForceHandleCtrlZ(bool force = true);

  // Post tasks to be executed by the loop.

  /// @brief Add a task to the main loop.
  /// It will be executed later, after every other scheduled tasks.
  void Post(Task task);

  /// @brief Add an event to the main loop.
  /// It will be executed later, after every other scheduled events.
  void PostEvent(Event event);

  /// @brief Add a task to the main loop.
  /// It will be executed later, after every other scheduled tasks.
  static void PostEventOrExecute(Closure closure);

  /// @brief Add a task to draw the screen one more time, until all the
  /// animations are done.
  void RequestAnimationFrame();

  // Selection API:

  /// @brief Try to get the unique lock about being able to capture the mouse.
  /// @return A unique lock if the mouse is not already captured, otherwise a
  /// null.
  CapturedMouse CaptureMouse();

  /// @brief Returns the content of the current selection.
  std::string GetSelection();

  /// @brief Set a callback that will be called when the selection changes.
  void SelectionChange(std::function<void()> callback);

  // Terminal info.

  /// @brief Return the terminal name.
  const std::string& TerminalName() const;

  /// @brief Return the terminal version.
  int TerminalVersion() const;

  /// @brief Return the terminal emulator name.
  const std::string& TerminalEmulatorName() const;

  /// @brief Return the terminal emulator version.
  const std::string& TerminalEmulatorVersion() const;

  /// @brief Return the terminal capabilities.
  const std::vector<int>& TerminalCapabilities() const;

  /// @brief Return the names of the terminal capabilities.
  std::vector<std::string> TerminalCapabilityNames() const;

 private:
  void ExitNow();
  void Install();
  void Uninstall();

  void PreMain();
  void PostMain();

  /// @brief Return whether the main loop has been quit.
  bool HasQuitted();
  void RunOnce(const Component& component);
  void RunOnceBlocking(Component component);

  void HandleTask(Component component, Task& task);
  bool HandleSelection(bool handled, Event event);
  void Draw(Component component);
  std::string ResetCursorPosition();

  void RequestCursorPosition(bool force = false);

  void TerminalSend(std::string_view);
  void TerminalFlush();

  void InstallPipedInputHandling();
  void InstallTerminalInfo();

  void Signal(int signal);

  size_t FetchTerminalEvents();

  void PostAnimationTask();

  struct Internal;
  explicit App(std::unique_ptr<Internal> internal, int dimx, int dimy);

  std::unique_ptr<Internal> internal_;

  friend class Loop;

 public:
  class Private {
   public:
    static void Signal(App& s, int signal) { s.Signal(signal); }
  };
  friend Private;
};

}  // namespace ftxui




#ifdef min
#error \
    "The macro 'min' is defined, which conflicts with the standard C++ library and FTXUI. This is often caused by including <windows.h>. To fix this, add '#define NOMINMAX' before including <windows.h>, or pass '/DNOMINMAX' as a compiler flag."
#endif

#ifdef max
#error \
    "The macro 'max' is defined, which conflicts with the standard C++ library and FTXUI. This is often caused by including <windows.h>. To fix this, add '#define NOMINMAX' before including <windows.h>, or pass '/DNOMINMAX' as a compiler flag."
#endif








#ifdef DrawText
// Workaround for WinUsr.h (via Windows.h) defining macros that break things.
// https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-drawtext
#undef DrawText
#endif

namespace ftxui {

/// @brief Canvas is a drawable buffer associated with drawing operations.
///
/// Canvas is a drawable area that can be used to create complex graphics. It
/// supports drawing points, lines, circles, ellipses, text, and images using
/// braille, block, or normal characters.
///
/// Note: A terminal contains cells. A cells is a unit of:
/// - 2x4 braille characters (1x1 pixel)
/// - 2x2 block characters (2x2 pixels)
/// - 2x4 normal characters (2x4 pixels)
///
/// You need to multiply the x coordinate by 2 and the y coordinate by 4 to
/// get the correct position in the terminal.
///
/// @ingroup dom
struct FTXUI_EXPORT(DOM) Canvas {
 public:
  Canvas() = default;
  Canvas(int width, int height);

  // Getters:
  int width() const { return width_; }
  int height() const { return height_; }
  Cell GetCell(int x, int y) const;
  // [Deprecated] alias for GetCell.
  Cell GetPixel(int x, int y) const { return GetCell(x, y); }

  using Stylizer = std::function<void(Cell&)>;

  // Draws using braille characters --------------------------------------------
  void DrawPointOn(int x, int y);
  void DrawPointOff(int x, int y);
  void DrawPointToggle(int x, int y);
  void DrawPoint(int x, int y, bool value);
  void DrawPoint(int x, int y, bool value, const Stylizer& s);
  void DrawPoint(int x, int y, bool value, const Color& color);
  void DrawPointLine(int x1, int y1, int x2, int y2);
  void DrawPointLine(int x1, int y1, int x2, int y2, const Stylizer& s);
  void DrawPointLine(int x1, int y1, int x2, int y2, const Color& color);
  void DrawPointCircle(int x, int y, int radius);
  void DrawPointCircle(int x, int y, int radius, const Stylizer& s);
  void DrawPointCircle(int x, int y, int radius, const Color& color);
  void DrawPointCircleFilled(int x, int y, int radius);
  void DrawPointCircleFilled(int x, int y, int radius, const Stylizer& s);
  void DrawPointCircleFilled(int x, int y, int radius, const Color& color);
  void DrawPointEllipse(int x, int y, int r1, int r2);
  void DrawPointEllipse(int x, int y, int r1, int r2, const Color& color);
  void DrawPointEllipse(int x, int y, int r1, int r2, const Stylizer& s);
  void DrawPointEllipseFilled(int x, int y, int r1, int r2);
  void DrawPointEllipseFilled(int x, int y, int r1, int r2, const Color& color);
  void DrawPointEllipseFilled(int x, int y, int r1, int r2, const Stylizer& s);

  // Draw using box characters -------------------------------------------------
  // Block are of size 1x2. y is considered to be a multiple of 2.
  void DrawBlockOn(int x, int y);
  void DrawBlockOff(int x, int y);
  void DrawBlockToggle(int x, int y);
  void DrawBlock(int x, int y, bool value);
  void DrawBlock(int x, int y, bool value, const Stylizer& s);
  void DrawBlock(int x, int y, bool value, const Color& color);
  void DrawBlockLine(int x1, int y1, int x2, int y2);
  void DrawBlockLine(int x1, int y1, int x2, int y2, const Stylizer& s);
  void DrawBlockLine(int x1, int y1, int x2, int y2, const Color& color);
  void DrawBlockCircle(int x1, int y1, int radius);
  void DrawBlockCircle(int x1, int y1, int radius, const Stylizer& s);
  void DrawBlockCircle(int x1, int y1, int radius, const Color& color);
  void DrawBlockCircleFilled(int x1, int y1, int radius);
  void DrawBlockCircleFilled(int x1, int y1, int radius, const Stylizer& s);
  void DrawBlockCircleFilled(int x1, int y1, int radius, const Color& color);
  void DrawBlockEllipse(int x1, int y1, int r1, int r2);
  void DrawBlockEllipse(int x1, int y1, int r1, int r2, const Stylizer& s);
  void DrawBlockEllipse(int x1, int y1, int r1, int r2, const Color& color);
  void DrawBlockEllipseFilled(int x1, int y1, int r1, int r2);
  void DrawBlockEllipseFilled(int x1,
                              int y1,
                              int r1,
                              int r2,
                              const Stylizer& s);
  void DrawBlockEllipseFilled(int x1,
                              int y1,
                              int r1,
                              int r2,
                              const Color& color);

  // Draw using normal characters ----------------------------------------------
  // Draw using character of size 2x4 at position (x,y)
  // x is considered to be a multiple of 2.
  // y is considered to be a multiple of 4.
  void DrawText(int x, int y, std::string_view value);
  void DrawText(int x, int y, std::string_view value, const Color& color);
  void DrawText(int x, int y, std::string_view value, const Stylizer& style);

  // Draw using directly pixels or images --------------------------------------
  // x is considered to be a multiple of 2.
  // y is considered to be a multiple of 4.
  void DrawCell(int x, int y, const Cell&);
  void DrawSurface(int x, int y, const Surface&);

  // [Deprecated] alias for DrawCell.
  void DrawPixel(int x, int y, const Cell& cell) { DrawCell(x, y, cell); }
  // [Deprecated] alias for DrawSurface.
  void DrawImage(int x, int y, const Surface& s) { DrawSurface(x, y, s); }

  // Decorator:
  // x is considered to be a multiple of 2.
  // y is considered to be a multiple of 4.
  void Style(int x, int y, const Stylizer& style);

 private:
  bool IsIn(int x, int y) const {
    return x >= 0 && x < width_ && y >= 0 && y < height_;
  }

  enum CellType {
    kCell,     // Units of size 2x4
    kBlock,    // Units of size 2x2
    kBraille,  // Units of size 1x1
  };

  struct CanvasCell {
    CellType type = kCell;
    Cell content;
  };

  struct XY {
    int x;
    int y;
    bool operator==(const XY& other) const {
      return x == other.x && y == other.y;
    }
  };

  struct XYHash {
    size_t operator()(const XY& xy) const {
      constexpr size_t shift = 1024;
      return size_t(xy.x) * shift + size_t(xy.y);
    }
  };

  int width_ = 0;
  int height_ = 0;
  std::unordered_map<XY, CanvasCell, XYHash> storage_;
};

}  // namespace ftxui



namespace ftxui {

/// @brief Direction is an enumeration that represents the four cardinal
/// directions.
///
/// @ingroup dom
enum class Direction : uint8_t {
  Up = 0,
  Down = 1,
  Left = 2,
  Right = 3,
};

}  // namespace ftxui




/*
  This replicate the CSS flexbox model.
  See guide for documentation:
  https://css-tricks.com/snippets/css/a-guide-to-flexbox/
*/

namespace ftxui {

/// @brief FlexboxConfig is a configuration structure that defines the layout
/// properties for a flexbox container.
//
/// It allows you to specify the direction of the flex items, whether they
/// should wrap, how they should be justified along the main axis, and how
/// they should be aligned along the cross axis.
/// It also includes properties for gaps between flex items in both the
/// main and cross axes.
/// This structure is used to configure the layout behavior of flexbox
/// containers in a terminal user interface.
///
/// @ingroup dom
struct FTXUI_EXPORT(DOM) FlexboxConfig {
  /// This establishes the main-axis, thus defining the direction flex items are
  /// placed in the flex container. Flexbox is (aside wrapping) single-direction
  /// layout concept. Think of flex items as primarily laying out either in
  /// horizontal rows or vertical columns.
  enum class Direction : uint8_t {
    Row,            ///< Flex items are laid out in a row.
    RowInversed,    ///< Flex items are laid out in a row, but in reverse order.
    Column,         ///< Flex items are laid out in a column.
    ColumnInversed  ///< Flex items are laid out in a column, but in reverse
                    ///< order.
  };
  Direction direction = Direction::Row;

  /// By default, flex items will all try to fit onto one line. You can change
  /// that and allow the items to wrap as needed with this property.
  enum class Wrap : uint8_t {
    NoWrap,        ///< Flex items will all try to fit onto one line.
    Wrap,          ///< Flex items will wrap onto multiple lines.
    WrapInversed,  ///< Flex items will wrap onto multiple lines, but in reverse
                   ///< order.
  };
  Wrap wrap = Wrap::Wrap;

  /// This defines the alignment along the main axis. It helps distribute extra
  /// free space leftover when either all the flex items on a line are
  /// inflexible, or are flexible but have reached their maximum size. It also
  /// exerts some control over the alignment of items when they overflow the
  /// line.
  enum class JustifyContent : uint8_t {
    /// Items are aligned to the start of flexbox's direction.
    FlexStart,
    /// Items are aligned to the end of flexbox's direction.
    FlexEnd,
    /// Items are centered along the line.
    Center,
    /// Items are stretched to fill the line.
    Stretch,
    /// Items are evenly distributed in the line; first item is on the start
    // line, last item on the end line
    SpaceBetween,
    /// Items are evenly distributed in the line with equal space around them.
    /// Note that visually the spaces aren’t equal, since all the items have
    /// equal space on both sides. The first item will have one unit of space
    /// against the container edge, but two units of space between the next item
    /// because that next item has its own spacing that applies.
    SpaceAround,
    /// Items are distributed so that the spacing between any two items (and the
    /// space to the edges) is equal.
    SpaceEvenly,
  };
  JustifyContent justify_content = JustifyContent::FlexStart;

  /// This defines the default behavior for how flex items are laid out along
  /// the cross axis on the current line. Think of it as the justify-content
  /// version for the cross-axis (perpendicular to the main-axis).
  enum class AlignItems : uint8_t {
    FlexStart,  ///< items are placed at the start of the cross axis.
    FlexEnd,    ///< items are placed at the end of the cross axis.
    Center,     ///< items are centered along the cross axis.
    Stretch,    ///< items are stretched to fill the cross axis.
  };
  AlignItems align_items = AlignItems::FlexStart;

  // This aligns a flex container’s lines within when there is extra space in
  // the cross-axis, similar to how justify-content aligns individual items
  // within the main-axis.
  enum class AlignContent : uint8_t {
    FlexStart,     ///< items are placed at the start of the cross axis.
    FlexEnd,       ///< items are placed at the end of the cross axis.
    Center,        ///< items are centered along the cross axis.
    Stretch,       ///< items are stretched to fill the cross axis.
    SpaceBetween,  ///< items are evenly distributed in the cross axis.
    SpaceAround,   ///< tems evenly distributed with equal space around each
                   ///< line.
    SpaceEvenly,  ///< items are evenly distributed in the cross axis with equal
                  ///< space around them.
  };
  AlignContent align_content = AlignContent::FlexStart;

  int gap_x = 0;
  int gap_y = 0;

  // Constructor pattern. For chained use like:
  // ```
  // FlexboxConfig()
  //    .Set(FlexboxConfig::Direction::Row)
  //    .Set(FlexboxConfig::Wrap::Wrap);
  // ```
  FlexboxConfig& Set(FlexboxConfig::Direction);
  FlexboxConfig& Set(FlexboxConfig::Wrap);
  FlexboxConfig& Set(FlexboxConfig::JustifyContent);
  FlexboxConfig& Set(FlexboxConfig::AlignItems);
  FlexboxConfig& Set(FlexboxConfig::AlignContent);
  FlexboxConfig& SetGap(int gap_x, int gap_y);
};

}  // namespace ftxui




namespace ftxui {

/// @brief A class representing the settings for linear-gradient color effect.
///
/// Example:
/// ```cpp
/// LinearGradient()
///    .Angle(45)
///    .Stop(Color::Red, 0.0)
///    .Stop(Color::Green, 0.5)
///    .Stop(Color::Blue, 1.0);
/// ```
///
/// There are also shorthand constructors:
/// ```cpp
/// LinearGradient(Color::Red, Color::Blue);
/// LinearGradient(45, Color::Red, Color::Blue);
/// ```
///
/// @ingroup dom
struct FTXUI_EXPORT(DOM) LinearGradient {
  float angle = 0.f;

  /// A stop is a color at a specific position in the gradient.
  /// The position is a value between 0.0 and 1.0,
  /// where 0.0 is the start of the gradient
  /// and 1.0 is the end of the gradient.
  struct Stop {
    Color color = Color::Default;
    std::optional<float> position;
  };
  std::vector<Stop> stops;

  // Simple constructor
  LinearGradient();
  LinearGradient(Color begin, Color end);
  LinearGradient(float angle, Color begin, Color end);

  // Modifier using the builder pattern.
  LinearGradient& Angle(float angle);
  LinearGradient& Stop(Color color, float position);
  LinearGradient& Stop(Color color);
};

}  // namespace ftxui





namespace ftxui {
class Node;

/// @brief Requirement is a structure that defines the layout requirements for a
/// Node in the terminal user interface.
///
/// It specifies the minimum size required to fully draw the element,
/// @ingroup dom
struct FTXUI_EXPORT(DOM) Requirement {
  // The required size to fully draw the element.
  int min_x = 0;
  int min_y = 0;

  // How much flexibility is given to the component.
  int flex_grow_x = 0;
  int flex_grow_y = 0;
  int flex_shrink_x = 0;
  int flex_shrink_y = 0;

  // Focus management to support the frame/focus/select element.
  struct Focused {
    bool enabled = false;
    Box box;
    Node* node = nullptr;
    Screen::Cursor::Shape cursor_shape = Screen::Cursor::Shape::Hidden;

    // Internal for interactions with components.
    bool component_active = false;
    bool component_focused = false;

    // Return whether this requirement should be preferred over the other.
    bool Prefer(const Focused& other) const {
      if (!other.enabled) {
        return false;
      }
      if (!enabled) {
        return true;
      }
      if (other.component_focused != component_focused) {
        return other.component_focused;
      }

      return other.component_active && !component_active;
    }
  };
  Focused focused;
};

}  // namespace ftxui




namespace ftxui {

/// @brief Represents a selection in a terminal user interface.
///
/// Selection is a class that represents the two endpoints of a selection in a
/// terminal user interface.
///
/// @ingroup dom
class FTXUI_EXPORT(DOM) Selection {
 public:
  Selection();  // Empty selection.
  Selection(int start_x, int start_y, int end_x, int end_y);

  const Box& GetBox() const;

  Selection SaturateHorizontal(Box box);
  Selection SaturateVertical(Box box);
  bool IsEmpty() const { return empty_; }

  void AddPart(std::string_view part, int y, int left, int right);
  std::string GetParts() { return parts_.str(); }

 private:
  Selection(int start_x, int start_y, int end_x, int end_y, Selection* parent);

  const int start_x_ = 0;
  const int start_y_ = 0;
  const int end_x_ = 0;
  const int end_y_ = 0;
  const Box box_ = {};
  Selection* const parent_ = this;
  const bool empty_ = true;
  std::stringstream parts_;

  // The position of the last inserted part.
  int x_ = 0;
  int y_ = 0;
};

}  // namespace ftxui


namespace ftxui {

class Node;
class Screen;

using Element = std::shared_ptr<Node>;
using Elements = std::vector<Element>;

/// @brief Node is the base class for all elements in the DOM tree.
///
/// It represents a single node in the document object model (DOM) and provides
/// the basic structure for layout and rendering.
/// It contains methods for computing layout requirements, setting the box
/// dimensions, selecting content, rendering to the screen, and checking the
/// layout status.
/// It typically contains child elements, which are also instances of Node.
///
/// Users are expected to derive from this class to create custom elements.
///
/// A list of builtin elements can be found in the `elements.hpp` file.
///
/// @ingroup dom
class FTXUI_EXPORT(DOM) Node {
 public:
  Node();
  explicit Node(Elements children);
  Node(const Node&) = delete;
  Node(const Node&&) = delete;
  Node& operator=(const Node&) = delete;
  Node& operator=(const Node&&) = delete;

  virtual ~Node();

  // Step 1: Compute layout requirement. Tell parent what dimensions this
  //         element wants to be.
  //         Propagated from Children to Parents.
  virtual void ComputeRequirement();
  Requirement requirement() { return requirement_; }

  // Step 2: Assign this element its final dimensions.
  //         Propagated from Parents to Children.
  virtual void SetBox(Box box);

  // Step 3: (optional) Selection
  //         Propagated from Parents to Children.
  virtual void Select(Selection& selection);

  // Step 4: Draw this element.
  virtual void Render(Screen& screen);

  virtual std::string GetSelectedContent(Selection& selection);

  // Layout may not resolve within a single iteration for some elements. This
  // allows them to request additional iterations. This signal must be
  // forwarded to children at least once.
  struct Status {
    int iteration = 0;
    bool need_iteration = false;
  };
  virtual void Check(Status* status);

  // ABI Reserve:
  virtual void Reserved1();
  virtual void Reserved2();
  virtual void Reserved3();
  virtual void Reserved4();
  virtual void Reserved5();
  virtual void Reserved6();
  virtual void Reserved7();
  virtual void Reserved8();

  friend FTXUI_EXPORT(DOM) void Render(Screen& screen, Node* node, Selection& selection);

 protected:
  Elements children_;
  Requirement requirement_;
  Box box_;
};

FTXUI_EXPORT(DOM) void Render(Screen& screen, const Element& element);
FTXUI_EXPORT(DOM) void Render(Screen& screen, Node* node);
FTXUI_EXPORT(DOM)
void Render(Screen& screen, Node* node, Selection& selection);
FTXUI_EXPORT(DOM)
std::string GetNodeSelectedContent(Screen& screen,
                                   Node* node,
                                   Selection& selection);

}  // namespace ftxui





namespace ftxui {
FTXUI_EXPORT(SCREEN) std::string to_string(std::wstring_view s);
FTXUI_EXPORT(SCREEN) std::wstring to_wstring(std::string_view s);

template <typename T>
std::wstring to_wstring(T s) {
  return to_wstring(std::string_view(std::to_string(s)));
}
inline std::wstring to_wstring(const std::string& s) {
  return to_wstring(std::string_view(s));
}
template <>
inline std::wstring to_wstring(const char* s) {
  return to_wstring(std::string_view(s));
}

FTXUI_EXPORT(SCREEN) int string_width(std::string_view);

// Split the string into a its glyphs. An empty one is inserted ater fullwidth
// ones.
FTXUI_EXPORT(SCREEN)
std::vector<std::string> Utf8ToGlyphs(std::string_view input);

// Map every cells drawn by |input| to their corresponding Glyphs. Half-size
// Glyphs takes one cell, full-size Glyphs take two cells.
FTXUI_EXPORT(SCREEN)
std::vector<int> CellToGlyphIndex(std::string_view input);

}  // namespace ftxui



namespace ftxui {

/// @brief An adapter. Own or reference an immutable object.
template <typename T>
class ConstRef {
 public:
  ConstRef() = default;

  // Owning constructors:
  ConstRef(T t) : variant_(std::move(t)) {}  // NOLINT

  // Referencing constructors:
  ConstRef(const T* t) : variant_(t) {}  // NOLINT

  ConstRef& operator=(ConstRef&&) noexcept = default;
  ConstRef(const ConstRef<T>&) = default;
  ConstRef(ConstRef<T>&&) noexcept = default;
  ~ConstRef() = default;

  // Make a "reseatable" reference
  ConstRef<T>& operator=(const ConstRef<T>&) = default;

  // Accessors:
  const T& operator()() const { return *Address(); }
  const T& operator*() const { return *Address(); }
  const T* operator->() const { return Address(); }

 private:
  std::variant<T, const T*> variant_ = T{};

  const T* Address() const {
    if (const T* t = std::get_if<T>(&variant_)) {
      return t;
    }
    return std::get<const T*>(variant_);
  }
};

/// @brief An adapter. Own or reference an mutable object.
template <typename T>
class Ref {
 public:
  Ref() = default;

  // Owning constructors:
  Ref(T t)
      : variant_(std::move(t)) {}  // NOLINT
                                   //
  // Referencing constructors:
  Ref(T* t)
      : variant_(t) {}  // NOLINT
                        //
  ~Ref() = default;
  Ref& operator=(Ref&&) noexcept = default;
  Ref(const Ref<T>&) = default;
  Ref(Ref<T>&&) noexcept = default;

  // Make a "reseatable" reference.
  Ref<T>& operator=(const Ref<T>&) = default;

  // Accessors:
  T& operator()() { return *Address(); }
  T& operator*() { return *Address(); }
  T* operator->() { return Address(); }
  const T& operator()() const { return *Address(); }
  const T& operator*() const { return *Address(); }
  const T* operator->() const { return Address(); }

 private:
  std::variant<T, T*> variant_ = T{};

  const T* Address() const {
    if (const T* t = std::get_if<T>(&variant_)) {
      return t;
    }
    return std::get<T*>(variant_);
  }
  T* Address() {
    if (T* t = std::get_if<T>(&variant_)) {
      return t;
    }
    return std::get<T*>(variant_);
  }
};

/// @brief An adapter. Own or reference a constant string. For convenience, this
/// class convert multiple mutable string toward a shared representation.
class FTXUI_EXPORT(SCREEN) StringRef : public Ref<std::string> {
 public:
  using Ref<std::string>::Ref;

  // Owning constructors:
  StringRef(const wchar_t* ref)  // NOLINT
      : StringRef(to_string(std::wstring(ref))) {}
  StringRef(const char* ref)  // NOLINT
      : StringRef(std::string(ref)) {}
  StringRef(std::string_view ref)  // NOLINT
      : StringRef(std::string(ref)) {}
  StringRef(std::wstring_view ref)  // NOLINT
      : StringRef(to_string(ref)) {}
};

/// @brief An adapter. Own or reference a constant string. For convenience, this
/// class convert multiple immutable string toward a shared representation.
class FTXUI_EXPORT(SCREEN) ConstStringRef : public ConstRef<std::string> {
 public:
  using ConstRef<std::string>::ConstRef;

  // Referencing constructors:
  ConstStringRef(const std::wstring* ref)  // NOLINT
      : ConstStringRef(to_string(*ref)) {}

  // Owning constructors:
  ConstStringRef(const std::wstring ref)  // NOLINT
      : ConstStringRef(to_string(ref)) {}
  ConstStringRef(std::wstring_view ref)  // NOLINT
      : ConstStringRef(to_string(ref)) {}
  ConstStringRef(const wchar_t* ref)  // NOLINT
      : ConstStringRef(to_string(std::wstring(ref))) {}
  ConstStringRef(const char* ref)  // NOLINT
      : ConstStringRef(std::string(ref)) {}
  ConstStringRef(std::string_view ref)  // NOLINT
      : ConstStringRef(std::string(ref)) {}
};

/// @brief An adapter. Reference a list of strings.
///
/// Supported input:
/// - `std::vector<std::string>`
/// - `std::vector<std::string>*`
/// - `std::vector<std::wstring>*`
/// - `Adapter*`
/// - `std::unique_ptr<Adapter>`
class FTXUI_EXPORT(SCREEN) ConstStringListRef {
 public:
  // Bring your own adapter:
  class Adapter {
   public:
    Adapter() = default;
    Adapter(const Adapter&) = default;
    Adapter& operator=(const Adapter&) = default;
    Adapter(Adapter&&) = default;
    Adapter& operator=(Adapter&&) = default;
    virtual ~Adapter() = default;
    virtual size_t size() const = 0;
    virtual std::string_view operator[](size_t i) const = 0;
  };
  using Variant = std::variant<const std::vector<std::string>,        //
                               const std::vector<std::string>*,       //
                               const std::vector<std::string_view>,   //
                               const std::vector<std::string_view>*,  //
                               const std::vector<std::wstring>*,      //
                               Adapter*,                              //
                               std::unique_ptr<Adapter>               //
                               >;

  ConstStringListRef() = default;
  ~ConstStringListRef() = default;
  ConstStringListRef& operator=(const ConstStringListRef&) = default;
  ConstStringListRef& operator=(ConstStringListRef&&) = default;
  ConstStringListRef(ConstStringListRef&&) = default;
  ConstStringListRef(const ConstStringListRef&) = default;

  ConstStringListRef(std::vector<std::string> value) {  // NOLINT
    variant_ = std::make_shared<Variant>(value);
  }
  ConstStringListRef(const std::vector<std::string>* value) {  // NOLINT
    variant_ = std::make_shared<Variant>(value);
  }
  ConstStringListRef(std::vector<std::string_view> value) {  // NOLINT
    variant_ = std::make_shared<Variant>(value);
  }
  ConstStringListRef(const std::vector<std::string_view>* value) {  // NOLINT
    variant_ = std::make_shared<Variant>(value);
  }
  ConstStringListRef(const std::vector<std::wstring>* value) {  // NOLINT
    variant_ = std::make_shared<Variant>(value);
  }
  ConstStringListRef(Adapter* adapter) {  // NOLINT
    variant_ = std::make_shared<Variant>(adapter);
  }
  template <typename AdapterType>
  ConstStringListRef(std::unique_ptr<AdapterType> adapter) {  // NOLINT
    variant_ = std::make_shared<Variant>(
        static_cast<std::unique_ptr<Adapter>>(std::move(adapter)));
  }

  size_t size() const {
    return variant_ ? std::visit(SizeVisitor(), *variant_) : 0;
  }

  std::string_view operator[](size_t i) const {
    return variant_ ? std::visit(IndexedGetter{i}, *variant_) : "";
  }

 private:
  struct IndexedGetter {
    size_t i;
    std::string_view operator()(const std::vector<std::string>& v) const {
      return v[i];
    }
    std::string_view operator()(const std::vector<std::string>* v) const {
      return (*v)[i];
    }
    std::string_view operator()(const std::vector<std::string_view>& v) const {
      return v[i];
    }
    std::string_view operator()(const std::vector<std::string_view>* v) const {
      return (*v)[i];
    }
    std::string_view operator()(
        [[maybe_unused]] const std::vector<std::wstring>* v) const {
      return "";  // Temporary fix: Cannot return a view to a temporary
                  // conversion.
    }
    std::string_view operator()(Adapter* v) const { return (*v)[i]; }
    std::string_view operator()(const std::unique_ptr<Adapter>& v) const {
      return (*v)[i];
    }
  };

  struct SizeVisitor {
    size_t operator()(const std::vector<std::string>& v) const {
      return v.size();
    }
    size_t operator()(const std::vector<std::string>* v) const {
      return v->size();
    }
    size_t operator()(const std::vector<std::string_view>& v) const {
      return v.size();
    }
    size_t operator()(const std::vector<std::string_view>* v) const {
      return v->size();
    }
    size_t operator()(const std::vector<std::wstring>* v) const {
      return v->size();
    }
    size_t operator()(const Adapter* v) const { return v->size(); }
    size_t operator()(const std::unique_ptr<Adapter>& v) const {
      return v->size();
    }
  };

  std::shared_ptr<Variant> variant_;
};

}  // namespace ftxui


namespace ftxui {
class Node;
using Element = std::shared_ptr<Node>;
using Elements = std::vector<Element>;
using Decorator = std::function<Element(Element)>;
using GraphFunction = std::function<std::vector<int>(int, int)>;

/// @brief BorderStyle is an enumeration that represents the different styles
/// of borders that can be applied to elements in the terminal UI.
///
/// BorderStyle is an enumeration that represents the different styles of
/// borders that can be applied to elements in the terminal UI.
/// It is used to define the visual appearance of borders around elements,
/// such as windows, frames, or separators.
/// @ingroup dom
enum BorderStyle : uint8_t {
  LIGHT,
  DASHED,
  HEAVY,
  DOUBLE,
  ROUNDED,
  EMPTY,
};

// Pipe elements into decorator together.
// For instance the next lines are equivalents:
// -> text("ftxui") | bold | underlined
// -> underlined(bold(text("FTXUI")))
FTXUI_EXPORT(DOM) Element operator|(Element, Decorator);
FTXUI_EXPORT(DOM) Element& operator|=(Element&, Decorator);
FTXUI_EXPORT(DOM) Elements operator|(Elements, Decorator);
FTXUI_EXPORT(DOM) Decorator operator|(Decorator, Decorator);

// --- Widget ---
FTXUI_EXPORT(DOM) Element text(std::string_view text);
FTXUI_EXPORT(DOM) Element vtext(std::string_view text);
FTXUI_EXPORT(DOM) Element separator();
FTXUI_EXPORT(DOM) Element separatorLight();
FTXUI_EXPORT(DOM) Element separatorDashed();
FTXUI_EXPORT(DOM) Element separatorHeavy();
FTXUI_EXPORT(DOM) Element separatorDouble();
FTXUI_EXPORT(DOM) Element separatorEmpty();
FTXUI_EXPORT(DOM) Element separatorStyled(BorderStyle);
FTXUI_EXPORT(DOM) Element separator(Cell);
FTXUI_EXPORT(DOM) Element separatorCharacter(std::string_view);
FTXUI_EXPORT(DOM)
Element separatorHSelector(float left,
                           float right,
                           Color unselected_color,
                           Color selected_color);
FTXUI_EXPORT(DOM)
Element separatorVSelector(float up,
                           float down,
                           Color unselected_color,
                           Color selected_color);
FTXUI_EXPORT(DOM) Element gauge(float progress);
FTXUI_EXPORT(DOM) Element gaugeLeft(float progress);
FTXUI_EXPORT(DOM) Element gaugeRight(float progress);
FTXUI_EXPORT(DOM) Element gaugeUp(float progress);
FTXUI_EXPORT(DOM) Element gaugeDown(float progress);
FTXUI_EXPORT(DOM) Element gaugeDirection(float progress, Direction direction);
FTXUI_EXPORT(DOM) Element border(Element);
FTXUI_EXPORT(DOM) Element borderLight(Element);
FTXUI_EXPORT(DOM) Element borderDashed(Element);
FTXUI_EXPORT(DOM) Element borderHeavy(Element);
FTXUI_EXPORT(DOM) Element borderDouble(Element);
FTXUI_EXPORT(DOM) Element borderRounded(Element);
FTXUI_EXPORT(DOM) Element borderEmpty(Element);
FTXUI_EXPORT(DOM) Decorator borderStyled(BorderStyle);
FTXUI_EXPORT(DOM) Decorator borderStyled(BorderStyle, Color);
FTXUI_EXPORT(DOM) Decorator borderStyled(Color);
FTXUI_EXPORT(DOM) Decorator borderWith(const Cell&);
FTXUI_EXPORT(DOM)
Element window(Element title, Element content, BorderStyle border = ROUNDED);
FTXUI_EXPORT(DOM) Element spinner(int charset_index, size_t image_index);
FTXUI_EXPORT(DOM) Element paragraph(std::string_view text);
FTXUI_EXPORT(DOM) Element paragraphAlignLeft(std::string_view text);
FTXUI_EXPORT(DOM) Element paragraphAlignRight(std::string_view text);
FTXUI_EXPORT(DOM) Element paragraphAlignCenter(std::string_view text);
FTXUI_EXPORT(DOM) Element paragraphAlignJustify(std::string_view text);
FTXUI_EXPORT(DOM) Element graph(GraphFunction);
FTXUI_EXPORT(DOM) Element emptyElement();
FTXUI_EXPORT(DOM) Element canvas(ConstRef<Canvas>);
FTXUI_EXPORT(DOM)
Element canvas(int width, int height, std::function<void(Canvas&)>);
FTXUI_EXPORT(DOM) Element canvas(std::function<void(Canvas&)>);

// -- Decorator ---
FTXUI_EXPORT(DOM) Element bold(Element);
FTXUI_EXPORT(DOM) Element dim(Element);
FTXUI_EXPORT(DOM) Element italic(Element);
FTXUI_EXPORT(DOM) Element inverted(Element);
FTXUI_EXPORT(DOM) Element underlined(Element);
FTXUI_EXPORT(DOM) Element underlinedDouble(Element);
FTXUI_EXPORT(DOM) Element blink(Element);
FTXUI_EXPORT(DOM) Element strikethrough(Element);
FTXUI_EXPORT(DOM) Decorator color(Color);
FTXUI_EXPORT(DOM) Decorator bgcolor(Color);
FTXUI_EXPORT(DOM) Decorator color(const LinearGradient&);
FTXUI_EXPORT(DOM) Decorator bgcolor(const LinearGradient&);
FTXUI_EXPORT(DOM) Element color(Color, Element);
FTXUI_EXPORT(DOM) Element bgcolor(Color, Element);
FTXUI_EXPORT(DOM) Element color(const LinearGradient&, Element);
FTXUI_EXPORT(DOM) Element bgcolor(const LinearGradient&, Element);
FTXUI_EXPORT(DOM) Decorator focusPosition(int x, int y);
FTXUI_EXPORT(DOM) Decorator focusPositionRelative(float x, float y);
FTXUI_EXPORT(DOM) Element automerge(Element child);
FTXUI_EXPORT(DOM) Decorator hyperlink(std::string_view link);
FTXUI_EXPORT(DOM) Element hyperlink(std::string_view link, Element child);
FTXUI_EXPORT(DOM) Element selectionStyleReset(Element);
FTXUI_EXPORT(DOM) Decorator selectionColor(Color foreground);
FTXUI_EXPORT(DOM) Decorator selectionBackgroundColor(Color foreground);
FTXUI_EXPORT(DOM) Decorator selectionForegroundColor(Color foreground);
FTXUI_EXPORT(DOM) Decorator selectionStyle(std::function<void(Cell&)> style);

// --- Layout is
// Horizontal, Vertical or stacked set of elements.
FTXUI_EXPORT(DOM) Element hbox(Elements);
FTXUI_EXPORT(DOM) Element vbox(Elements);
FTXUI_EXPORT(DOM) Element dbox(Elements);
FTXUI_EXPORT(DOM)
Element flexbox(Elements, FlexboxConfig config = FlexboxConfig());
FTXUI_EXPORT(DOM) Element gridbox(std::vector<Elements> lines);

FTXUI_EXPORT(DOM)
Element hflow(Elements);  // Helper: default flexbox with row direction.
FTXUI_EXPORT(DOM)
Element vflow(Elements);  // Helper: default flexbox with column direction.

// -- Flexibility ---
// Define how to share the remaining space when not all of it is used inside a
// container.
FTXUI_EXPORT(DOM) Element flex(Element);  // Expand/Minimize if possible/needed.
FTXUI_EXPORT(DOM) Element flex_grow(Element);    // Expand element if possible.
FTXUI_EXPORT(DOM) Element flex_shrink(Element);  // Minimize element if needed.
FTXUI_EXPORT(DOM) Decorator flex_factor(int grow, int shrink);
FTXUI_EXPORT(DOM) Decorator flex_grow_factor(int grow);
FTXUI_EXPORT(DOM) Decorator flex_shrink_factor(int shrink);

FTXUI_EXPORT(DOM)
Element xflex(Element);  // Expand/Minimize if possible/needed on X axis.
FTXUI_EXPORT(DOM)
Element xflex_grow(Element);  // Expand element if possible on X axis.
FTXUI_EXPORT(DOM)
Element xflex_shrink(Element);  // Minimize element if needed on X axis.
FTXUI_EXPORT(DOM) Decorator xflex_factor(int grow, int shrink);
FTXUI_EXPORT(DOM) Decorator xflex_grow_factor(int grow);
FTXUI_EXPORT(DOM) Decorator xflex_shrink_factor(int shrink);

FTXUI_EXPORT(DOM)
Element yflex(Element);  // Expand/Minimize if possible/needed on Y axis.
FTXUI_EXPORT(DOM)
Element yflex_grow(Element);  // Expand element if possible on Y axis.
FTXUI_EXPORT(DOM)
Element yflex_shrink(Element);  // Minimize element if needed on Y axis.
FTXUI_EXPORT(DOM) Decorator yflex_factor(int grow, int shrink);
FTXUI_EXPORT(DOM) Decorator yflex_grow_factor(int grow);
FTXUI_EXPORT(DOM) Decorator yflex_shrink_factor(int shrink);

FTXUI_EXPORT(DOM) Element notflex(Element);  // Reset the flex attribute.
FTXUI_EXPORT(DOM) Element filler();          // A blank expandable element.

// -- Size override;
enum WidthOrHeight : uint8_t { WIDTH, HEIGHT };
enum Constraint : uint8_t { LESS_THAN, EQUAL, GREATER_THAN };
FTXUI_EXPORT(DOM) Decorator size(WidthOrHeight, Constraint, int value);

// --- Frame ---
// A frame is a scrollable area. The internal area is potentially larger than
// the external one. The internal area is scrolled in order to make visible the
// focused element.
FTXUI_EXPORT(DOM) Element frame(Element);
FTXUI_EXPORT(DOM) Element xframe(Element);
FTXUI_EXPORT(DOM) Element yframe(Element);
FTXUI_EXPORT(DOM) Element focus(Element);
FTXUI_EXPORT(DOM) Element select(Element e);  // Deprecated - Alias for focus.

// --- Cursor ---
// Those are similar to `focus`, but also change the shape of the cursor.
FTXUI_EXPORT(DOM) Element focusCursorBlock(Element);
FTXUI_EXPORT(DOM) Element focusCursorBlockBlinking(Element);
FTXUI_EXPORT(DOM) Element focusCursorBar(Element);
FTXUI_EXPORT(DOM) Element focusCursorBarBlinking(Element);
FTXUI_EXPORT(DOM) Element focusCursorUnderline(Element);
FTXUI_EXPORT(DOM) Element focusCursorUnderlineBlinking(Element);

// --- Misc ---
FTXUI_EXPORT(DOM) Element vscroll_indicator(Element);
FTXUI_EXPORT(DOM) Element hscroll_indicator(Element);
FTXUI_EXPORT(DOM) Decorator reflect(Box& box);
// Before drawing the |element| clear the pixel below. This is useful in
// combination with dbox.
FTXUI_EXPORT(DOM) Element clear_under(Element element);

// --- Util --------------------------------------------------------------------
FTXUI_EXPORT(DOM) Element hcenter(Element);
FTXUI_EXPORT(DOM) Element vcenter(Element);
FTXUI_EXPORT(DOM) Element center(Element);
FTXUI_EXPORT(DOM) Element align_right(Element);
FTXUI_EXPORT(DOM) Element nothing(Element element);

namespace Dimension {
FTXUI_EXPORT(DOM) Dimensions Fit(Element&, bool extend_beyond_screen = false);
}  // namespace Dimension

}  // namespace ftxui

// Make container able to take any number of children as input.

// IWYU pragma: private, include "ftxui/dom/elements.hpp"

namespace ftxui {

inline void Merge(Elements& container, Element element) {
  container.push_back(std::move(element));
}

// Turn a set of arguments into a vector.
template <class... Args>
Elements unpack(Args... args) {
  std::vector<Element> vec;
  (Merge(vec, std::move(args)), ...);
  return vec;
}

// Make |container| able to take any number of arguments.
#define TAKE_ANY_ARGS(container)                               \
  inline Element container(Element child) {                    \
    return container(unpack(std::move(child)));                \
  }                                                            \
                                                               \
  template <class... Args>                                     \
  inline Element container(Args... children) {                 \
    return container(unpack(std::forward<Args>(children)...)); \
  }                                                            \
                                                               \
  template <class Container>                                   \
  inline Element container(Container&& children) {             \
    Elements elements;                                         \
    for (auto& child : children) {                             \
      elements.push_back(std::move(child));                    \
    }                                                          \
    return container(std::move(elements));                     \
  }                                                            \
  template <>                                                  \
  inline Element container(std::stack<Element>&& children) {   \
    Elements elements;                                         \
    while (!children.empty()) {                                \
      elements.push_back(std::move(children.top()));           \
      children.pop();                                          \
    }                                                          \
    return container(std::move(elements));                     \
  }                                                            \
  template <>                                                  \
  inline Element container(std::queue<Element>&& children) {   \
    Elements elements;                                         \
    while (!children.empty()) {                                \
      elements.push_back(std::move(children.front()));         \
      children.pop();                                          \
    }                                                          \
    return container(std::move(elements));                     \
  }                                                            \
  template <>                                                  \
  inline Element container(std::deque<Element>&& children) {   \
    Elements elements;                                         \
    for (auto& child : children) {                             \
      elements.push_back(std::move(child));                    \
    }                                                          \
    return container(std::move(elements));                     \
  }

TAKE_ANY_ARGS(vbox)
TAKE_ANY_ARGS(hbox)
TAKE_ANY_ARGS(dbox)
TAKE_ANY_ARGS(hflow)
}  // namespace ftxui


// Include old definitions using wstring.


namespace ftxui {
FTXUI_EXPORT(DOM) Element text(std::wstring_view text);
FTXUI_EXPORT(DOM) Element vtext(std::wstring_view text);
}  // namespace ftxui


namespace ftxui {

class Delegate;
class Focus;
struct Event;

namespace animation {
class Params;
}  // namespace animation

class ComponentBase;
using Component = std::shared_ptr<ComponentBase>;
using Components = std::vector<Component>;

/// @brief It implement rendering itself as ftxui::Element. It implement
/// keyboard navigation by responding to ftxui::Event.
/// @ingroup component
class FTXUI_EXPORT(COMPONENT) ComponentBase {
 public:
  explicit ComponentBase(Components children);
  virtual ~ComponentBase();
  ComponentBase();

  // A component is not copyable/movable.
  ComponentBase(const ComponentBase&) = delete;
  ComponentBase(ComponentBase&&) = delete;
  ComponentBase& operator=(const ComponentBase&) = delete;
  ComponentBase& operator=(ComponentBase&&) = delete;

  // Component hierarchy:
  ComponentBase* Parent() const;
  Component& ChildAt(size_t i);
  size_t ChildCount() const;
  int Index() const;
  void Add(Component children);
  void Detach();
  void DetachAllChildren();

  // Renders the component.
  Element Render();

  // Override this function modify how `Render` works.
  virtual Element OnRender();

  // Handles an event.
  // By default, reduce on children with a lazy OR.
  //
  // Returns whether the event was handled or not.
  virtual bool OnEvent(Event);

  // Handle an animation step.
  virtual void OnAnimation(animation::Params& params);

  // Focus management ----------------------------------------------------------
  //
  // If this component contains children, this indicates which one is active,
  // nullptr if none is active.
  //
  // We say an element has the focus if the chain of ActiveChild() from the
  // root component contains this object.
  virtual Component ActiveChild();

  // Return true when the component contains focusable elements.
  // The non focusable Component will be skipped when navigating using the
  // keyboard.
  virtual bool Focusable() const;

  // Whether this is the active child of its parent.
  bool Active() const;
  // Whether all the ancestors are active.
  bool Focused() const;

  // Make the |child| to be the "active" one.
  virtual void SetActiveChild(ComponentBase* child);
  void SetActiveChild(Component child);

  // Configure all the ancestors to give focus to this component.
  void TakeFocus();

  // ABI Reserve:
  virtual void Reserved1();
  virtual void Reserved2();
  virtual void Reserved3();
  virtual void Reserved4();
  virtual void Reserved5();
  virtual void Reserved6();
  virtual void Reserved7();
  virtual void Reserved8();

 protected:
  CapturedMouse CaptureMouse(const Event& event);

  Components& children();
  const Components& children() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ftxui




namespace ftxui {

/// @brief arguments for transform from |ButtonOption|, |CheckboxOption|,
/// |RadioboxOption|, |MenuEntryOption|, |MenuOption|.
struct FTXUI_EXPORT(COMPONENT) EntryState {
  std::string label;  ///< The label to display.
  bool state;         ///< The state of the button/checkbox/radiobox
  bool active;        ///< Whether the entry is the active one.
  bool focused;       ///< Whether the entry is one focused by the user.
  int index;          ///< Index of the entry when applicable or -1.
};

/// @brief Option for the underline effect.
/// @ingroup component
struct FTXUI_EXPORT(COMPONENT) UnderlineOption {
  bool enabled = false;

  Color color_active = Color::White;
  Color color_inactive = Color::GrayDark;

  animation::easing::Function leader_function =
      animation::easing::QuadraticInOut;
  animation::easing::Function follower_function =
      animation::easing::QuadraticInOut;

  animation::Duration leader_duration = std::chrono::milliseconds(250);
  animation::Duration leader_delay = std::chrono::milliseconds(0);
  animation::Duration follower_duration = std::chrono::milliseconds(250);
  animation::Duration follower_delay = std::chrono::milliseconds(0);

  void SetAnimation(animation::Duration d, animation::easing::Function f);
  void SetAnimationDuration(animation::Duration d);
  void SetAnimationFunction(animation::easing::Function f);
  void SetAnimationFunction(animation::easing::Function f_leader,
                            animation::easing::Function f_follower);
};

/// @brief Option about a potentially animated color.
/// @ingroup component
struct FTXUI_EXPORT(COMPONENT) AnimatedColorOption {
  void Set(
      Color inactive,
      Color active,
      animation::Duration duration = std::chrono::milliseconds(250),
      animation::easing::Function function = animation::easing::QuadraticInOut);

  bool enabled = false;
  Color inactive;
  Color active;
  animation::Duration duration = std::chrono::milliseconds(250);
  animation::easing::Function function = animation::easing::QuadraticInOut;
};

struct FTXUI_EXPORT(COMPONENT) AnimatedColorsOption {
  AnimatedColorOption background;
  AnimatedColorOption foreground;
};

/// @brief Option for the MenuEntry component.
/// @ingroup component
struct FTXUI_EXPORT(COMPONENT) MenuEntryOption {
  ConstStringRef label = "MenuEntry";
  std::function<Element(const EntryState& state)> transform;
  AnimatedColorsOption animated_colors;
};

/// @brief Option for the Menu component.
/// @ingroup component
struct FTXUI_EXPORT(COMPONENT) MenuOption {
  // Standard constructors:
  static MenuOption Horizontal();
  static MenuOption HorizontalAnimated();
  static MenuOption Vertical();
  static MenuOption VerticalAnimated();
  static MenuOption Toggle();

  ConstStringListRef entries;  ///> The list of entries.
  Ref<int> selected = 0;       ///> The index of the selected entry.

  // Style:
  UnderlineOption underline;
  MenuEntryOption entries_option;
  Direction direction = Direction::Down;
  std::function<Element()> elements_prefix;
  std::function<Element()> elements_infix;
  std::function<Element()> elements_postfix;

  // Observers:
  std::function<void()> on_change;  ///> Called when the selected entry changes.
  std::function<void()> on_enter;   ///> Called when the user presses enter.
  Ref<int> focused_entry = 0;
};

/// @brief Option for the AnimatedButton component.
/// @ingroup component
struct FTXUI_EXPORT(COMPONENT) ButtonOption {
  // Standard constructors:
  static ButtonOption Ascii();
  static ButtonOption Simple();
  static ButtonOption Border();
  static ButtonOption Animated();
  static ButtonOption Animated(Color color);
  static ButtonOption Animated(Color background, Color foreground);
  static ButtonOption Animated(Color background,
                               Color foreground,
                               Color background_active,
                               Color foreground_active);

  ConstStringRef label = "Button";
  std::function<void()> on_click = [] {};

  // Style:
  std::function<Element(const EntryState&)> transform;
  AnimatedColorsOption animated_colors;
};

/// @brief Option for the Checkbox component.
/// @ingroup component
struct FTXUI_EXPORT(COMPONENT) CheckboxOption {
  // Standard constructors:
  static CheckboxOption Simple();

  ConstStringRef label = "Checkbox";

  Ref<bool> checked = false;

  // Style:
  std::function<Element(const EntryState&)> transform;

  // Observer:
  /// Called when the user change the state.
  std::function<void()> on_change = [] {};
};

/// @brief Used to define style for the Input component.
struct FTXUI_EXPORT(COMPONENT) InputState {
  Element element;
  bool hovered;         ///< Whether the input is hovered by the mouse.
  bool focused;         ///< Whether the input is focused by the user.
  bool is_placeholder;  ///< Whether the input is empty and displaying the
                        ///< placeholder.
};

/// @brief Option for the Input component.
/// @ingroup component
struct FTXUI_EXPORT(COMPONENT) InputOption {
  // A set of predefined styles:

  /// @brief Create the default input style:
  static InputOption Default();
  /// @brief A white on black style with high margins:
  static InputOption Spacious();

  /// The content of the input.
  StringRef content = "";

  /// The content of the input when it's empty.
  StringRef placeholder = "";

  // Style:
  std::function<Element(InputState)> transform;
  Ref<bool> password = false;  ///< Obscure the input content using '*'.
  Ref<bool> multiline = true;  ///< Whether the input can be multiline.
  Ref<bool> insert = true;     ///< Insert or overtype character mode.

  /// Called when the content changes.
  std::function<void()> on_change = [] {};
  /// Called when the user presses enter.
  std::function<void()> on_enter = [] {};

  // The char position of the cursor:
  Ref<int> cursor_position = 0;
};

/// @brief Option for the Radiobox component.
/// @ingroup component
struct FTXUI_EXPORT(COMPONENT) RadioboxOption {
  // Standard constructors:
  static RadioboxOption Simple();

  // Content:
  ConstStringListRef entries;
  Ref<int> selected = 0;

  // Style:
  std::function<Element(const EntryState&)> transform;

  // Observers:
  /// Called when the selected entry changes.
  std::function<void()> on_change = [] {};
  Ref<int> focused_entry = 0;
};

struct FTXUI_EXPORT(COMPONENT) ResizableSplitOption {
  Component main;
  Component back;
  Ref<Direction> direction = Direction::Left;
  Ref<int> main_size =
      (direction() == Direction::Left || direction() == Direction::Right) ? 20
                                                                          : 10;
  std::function<Element()> separator_func = [] { return ::ftxui::separator(); };

  // Constraints on main_size:
  Ref<int> min = 0;
  Ref<int> max = std::numeric_limits<int>::max();
};

// @brief Option for the `Slider` component.
// @ingroup component
template <typename T>
struct SliderOption {
  Ref<T> value;
  ConstRef<T> min = T(0);
  ConstRef<T> max = T(100);
  ConstRef<T> increment = (max() - min()) / 20;
  Direction direction = Direction::Right;
  Color color_active = Color::White;
  Color color_inactive = Color::GrayDark;
  std::function<void()> on_change;  ///> Called when `value` is updated.
};

/// @brief State passed to the `Window` component's render function.
/// @ingroup component
struct FTXUI_EXPORT(COMPONENT) WindowRenderState {
  Element inner;             ///< The element wrapped inside this window.
  const std::string& title;  ///< The title of the window.
  bool active = false;       ///< Whether the window is the active one.
  bool drag = false;         ///< Whether the window is being dragged.
  bool resize = false;       ///< Whether the window is being resized.
  bool hover_left = false;   ///< Whether the resizeable left side is hovered.
  bool hover_right = false;  ///< Whether the resizeable right side is hovered.
  bool hover_top = false;    ///< Whether the resizeable top side is hovered.
  bool hover_down = false;   ///< Whether the resizeable down side is hovered.
};

// @brief Option for the `Window` component.
// @ingroup component
struct FTXUI_EXPORT(COMPONENT) WindowOptions {
  Component inner;            ///< The component wrapped by this window.
  ConstStringRef title = "";  ///< The title displayed by this window.

  Ref<int> left = 0;     ///< The left side position of the window.
  Ref<int> top = 0;      ///< The top side position of the window.
  Ref<int> width = 20;   ///< The width of the window.
  Ref<int> height = 10;  ///< The height of the window.

  Ref<bool> resize_left = true;   ///< Can the left side be resized?
  Ref<bool> resize_right = true;  ///< Can the right side be resized?
  Ref<bool> resize_top = true;    ///< Can the top side be resized?
  Ref<bool> resize_down = true;   ///< Can the down side be resized?

  /// An optional function to customize how the window looks like:
  std::function<Element(const WindowRenderState&)> render;
};

/// @brief Option for the Dropdown component.
/// @ingroup component
/// A dropdown menu is a checkbox opening/closing a radiobox.
struct FTXUI_EXPORT(COMPONENT) DropdownOption {
  /// Whether the dropdown is open or closed:
  Ref<bool> open = false;
  // The options for the checkbox:
  CheckboxOption checkbox;
  // The options for the radiobox:
  RadioboxOption radiobox;
  // The transformation function:
  std::function<Element(bool open, Element checkbox, Element radiobox)>
      transform;
};

}  // namespace ftxui


namespace ftxui {
struct ButtonOption;
struct CheckboxOption;
struct Event;
struct InputOption;
struct MenuOption;
struct RadioboxOption;
struct MenuEntryOption;

template <class T, class... Args>
std::shared_ptr<T> Make(Args&&... args) {
  return std::make_shared<T>(std::forward<Args>(args)...);
}

// Pipe operator to decorate components.
using ComponentDecorator = std::function<Component(Component)>;
using ElementDecorator = std::function<Element(Element)>;
FTXUI_EXPORT(COMPONENT)
Component operator|(Component component, ComponentDecorator decorator);
FTXUI_EXPORT(COMPONENT)
Component operator|(Component component, ElementDecorator decorator);
FTXUI_EXPORT(COMPONENT)
Component& operator|=(Component& component, ComponentDecorator decorator);
FTXUI_EXPORT(COMPONENT)
Component& operator|=(Component& component, ElementDecorator decorator);

namespace Container {
FTXUI_EXPORT(COMPONENT) Component Vertical(Components children);
FTXUI_EXPORT(COMPONENT) Component Vertical(Components children, int* selector);
FTXUI_EXPORT(COMPONENT) Component Horizontal(Components children);
FTXUI_EXPORT(COMPONENT)
Component Horizontal(Components children, int* selector);
FTXUI_EXPORT(COMPONENT) Component Tab(Components children, int* selector);
FTXUI_EXPORT(COMPONENT) Component Stacked(Components children);
}  // namespace Container

FTXUI_EXPORT(COMPONENT) Component Button(ButtonOption options);
FTXUI_EXPORT(COMPONENT)
Component Button(ConstStringRef label,
                 std::function<void()> on_click,
                 ButtonOption options = ButtonOption::Simple());

FTXUI_EXPORT(COMPONENT) Component Checkbox(CheckboxOption options);
FTXUI_EXPORT(COMPONENT)
Component Checkbox(ConstStringRef label,
                   bool* checked,
                   CheckboxOption options = CheckboxOption::Simple());

FTXUI_EXPORT(COMPONENT) Component Input(InputOption options = {});
FTXUI_EXPORT(COMPONENT)
Component Input(StringRef content, InputOption options = {});
FTXUI_EXPORT(COMPONENT)
Component Input(StringRef content,
                StringRef placeholder,
                InputOption options = {});

FTXUI_EXPORT(COMPONENT) Component Menu(MenuOption options);
FTXUI_EXPORT(COMPONENT)
Component Menu(ConstStringListRef entries,
               int* selected_,
               MenuOption options = MenuOption::Vertical());
FTXUI_EXPORT(COMPONENT) Component MenuEntry(MenuEntryOption options);
FTXUI_EXPORT(COMPONENT)
Component MenuEntry(ConstStringRef label, MenuEntryOption options = {});

FTXUI_EXPORT(COMPONENT) Component Radiobox(RadioboxOption options);
FTXUI_EXPORT(COMPONENT)
Component Radiobox(ConstStringListRef entries,
                   int* selected_,
                   RadioboxOption options = {});

FTXUI_EXPORT(COMPONENT)
Component Dropdown(ConstStringListRef entries, int* selected);
FTXUI_EXPORT(COMPONENT) Component Dropdown(DropdownOption options);

FTXUI_EXPORT(COMPONENT)
Component Toggle(ConstStringListRef entries, int* selected);

// General slider constructor:
template <typename T>
Component Slider(SliderOption<T> options);

extern template FTXUI_EXPORT(COMPONENT) Component Slider(SliderOption<int8_t>);
extern template FTXUI_EXPORT(COMPONENT) Component Slider(SliderOption<int16_t>);
extern template FTXUI_EXPORT(COMPONENT) Component Slider(SliderOption<int32_t>);
extern template FTXUI_EXPORT(COMPONENT) Component Slider(SliderOption<int64_t>);

extern template FTXUI_EXPORT(COMPONENT) Component Slider(SliderOption<uint8_t>);
extern template FTXUI_EXPORT(COMPONENT) Component
    Slider(SliderOption<uint16_t>);
extern template FTXUI_EXPORT(COMPONENT) Component
    Slider(SliderOption<uint32_t>);
extern template FTXUI_EXPORT(COMPONENT) Component
    Slider(SliderOption<uint64_t>);

extern template FTXUI_EXPORT(COMPONENT) Component Slider(SliderOption<float>);
extern template FTXUI_EXPORT(COMPONENT) Component Slider(SliderOption<double>);

// Shorthand without the `SliderOption` constructor:
FTXUI_EXPORT(COMPONENT)
Component Slider(ConstStringRef label,
                 Ref<int> value,
                 ConstRef<int> min = 0,
                 ConstRef<int> max = 100,
                 ConstRef<int> increment = 5);
FTXUI_EXPORT(COMPONENT)
Component Slider(ConstStringRef label,
                 Ref<float> value,
                 ConstRef<float> min = 0.f,
                 ConstRef<float> max = 100.f,
                 ConstRef<float> increment = 5.f);
FTXUI_EXPORT(COMPONENT)
Component Slider(ConstStringRef label,
                 Ref<long> value,
                 ConstRef<long> min = 0L,
                 ConstRef<long> max = 100L,
                 ConstRef<long> increment = 5L);

FTXUI_EXPORT(COMPONENT)
Component ResizableSplit(ResizableSplitOption options);
FTXUI_EXPORT(COMPONENT)
Component ResizableSplitLeft(Component main, Component back, int* main_size);
FTXUI_EXPORT(COMPONENT)
Component ResizableSplitRight(Component main, Component back, int* main_size);
FTXUI_EXPORT(COMPONENT)
Component ResizableSplitTop(Component main, Component back, int* main_size);
FTXUI_EXPORT(COMPONENT)
Component ResizableSplitBottom(Component main, Component back, int* main_size);

FTXUI_EXPORT(COMPONENT)
Component Renderer(Component child, std::function<Element()>);
FTXUI_EXPORT(COMPONENT) Component Renderer(std::function<Element()>);
FTXUI_EXPORT(COMPONENT)
Component Renderer(std::function<Element(bool /* focused */)>);
FTXUI_EXPORT(COMPONENT) ComponentDecorator Renderer(ElementDecorator);

FTXUI_EXPORT(COMPONENT)
Component CatchEvent(Component child, std::function<bool(Event)>);
FTXUI_EXPORT(COMPONENT)
ComponentDecorator CatchEvent(std::function<bool(Event)> on_event);

FTXUI_EXPORT(COMPONENT) Component Maybe(Component, const bool* show);
FTXUI_EXPORT(COMPONENT) Component Maybe(Component, std::function<bool()>);
FTXUI_EXPORT(COMPONENT) ComponentDecorator Maybe(const bool* show);
FTXUI_EXPORT(COMPONENT) ComponentDecorator Maybe(std::function<bool()>);

FTXUI_EXPORT(COMPONENT)
Component Modal(Component main, Component modal, const bool* show_modal);
FTXUI_EXPORT(COMPONENT)
ComponentDecorator Modal(Component modal, const bool* show_modal);

FTXUI_EXPORT(COMPONENT)
Component Collapsible(ConstStringRef label,
                      Component child,
                      Ref<bool> show = false);

FTXUI_EXPORT(COMPONENT)
Component Hoverable(Component component, bool* hover);
FTXUI_EXPORT(COMPONENT)
Component Hoverable(Component component,
                    std::function<void()> on_enter,
                    std::function<void()> on_leave);
FTXUI_EXPORT(COMPONENT)
Component Hoverable(Component component,  //
                    std::function<void(bool)> on_change);
FTXUI_EXPORT(COMPONENT) ComponentDecorator Hoverable(bool* hover);
FTXUI_EXPORT(COMPONENT)
ComponentDecorator Hoverable(std::function<void()> on_enter,
                             std::function<void()> on_leave);
FTXUI_EXPORT(COMPONENT)
ComponentDecorator Hoverable(std::function<void(bool)> on_change);

FTXUI_EXPORT(COMPONENT) Component Window(WindowOptions option);

}  // namespace ftxui




namespace ftxui {
class ComponentBase;

using Component = std::shared_ptr<ComponentBase>;
class App;

/// @brief Loop is a class that manages the event loop for a component.
///
/// It is responsible for running the component, handling events, and
/// updating the screen.
///
/// The Loop class is designed to be used with an App object,
/// which represents the terminal screen.
///
/// **Example**
/// ```cpp
/// #include <ftxui/component/component.hpp>
/// #include <ftxui/component/app.hpp>
/// #include <ftxui/component/loop.hpp>
///
/// int main() {
///  auto screen = ftxui::App::TerminalOutput();
///  auto component = ftxui::Button("Click me", [] { ... });
///
///  ftxui::Loop loop(screen.get(), component);
///
///  // Either
///  loop.Run();  // Blocking until the component quits.
///
///  // Or
///  loop.RunOnce();  // Non-blocking, returns immediately.
///
///  // Or
///  loop.RunOnceBlocking();  // Blocking until handling one event.
///
///  // Or in a loop:
///  while (!loop.HasQuitted()) {
///    loop.RunOnce();
///
///    // Do something else like running a different library loop function.
///  }
/// }
/// ```
///
/// @ingroup component
class FTXUI_EXPORT(COMPONENT) Loop {
 public:
  Loop(App* screen, Component component);
  ~Loop();

  bool HasQuitted();
  void RunOnce();
  void RunOnceBlocking();
  void Run();

  // This class is non copyable/movable.
  Loop& operator=(Loop&&) = delete;
  Loop& operator=(const Loop&) = delete;
  Loop(Loop&&) = delete;
  Loop(const Loop&) = delete;

 private:
  App* screen_;
  Component component_;
};

}  // namespace ftxui



namespace ftxui {

// Deprecated
//
// Usage:
//
// Initialization:
// ---------------
//
// auto receiver = MakeReceiver<std:string>();
// auto sender_1= receiver->MakeSender();
// auto sender_2 = receiver->MakeSender();
//
// Then move the senders elsewhere, potentially in a different thread.
//
// On the producer side:
// ----------------------
// [thread 1] sender_1->Send("hello");
// [thread 2] sender_2->Send("world");
//
// On the consumer side:
// ---------------------
// char c;
// while(receiver->Receive(&c)) // Return true as long as there is a producer.
//   print(c)
//
// Receiver::Receive() returns true when there are no more senders.

// clang-format off
// Deprecated:
template<class T> class SenderImpl;
// Deprecated:
template<class T> class ReceiverImpl;
// Deprecated:

// Deprecated:
template<class T> using Sender = std::unique_ptr<SenderImpl<T>>;
// Deprecated:
template<class T> using Receiver = std::unique_ptr<ReceiverImpl<T>>;
// Deprecated:
template<class T> Receiver<T> MakeReceiver();
// clang-format on

// ---- Implementation part ----

template <class T>
// Deprecated:
class SenderImpl {
 public:
  SenderImpl(const SenderImpl&) = delete;
  SenderImpl(SenderImpl&&) = delete;
  SenderImpl& operator=(const SenderImpl&) = delete;
  SenderImpl& operator=(SenderImpl&&) = delete;
  void Send(T t) { receiver_->Receive(std::move(t)); }
  ~SenderImpl() { receiver_->ReleaseSender(); }

  Sender<T> Clone() { return receiver_->MakeSender(); }

 private:
  friend class ReceiverImpl<T>;
  explicit SenderImpl(ReceiverImpl<T>* consumer) : receiver_(consumer) {}
  ReceiverImpl<T>* receiver_;
};

template <class T>
class ReceiverImpl {
 public:
  Sender<T> MakeSender() {
    std::unique_lock<std::mutex> lock(mutex_);
    senders_++;
    return std::unique_ptr<SenderImpl<T>>(new SenderImpl<T>(this));
  }
  ReceiverImpl() = default;

  bool Receive(T* t) {
    while (senders_ || !queue_.empty()) {
      std::unique_lock<std::mutex> lock(mutex_);
      if (queue_.empty()) {
        notifier_.wait(lock);
      }
      if (queue_.empty()) {
        continue;
      }
      *t = std::move(queue_.front());
      queue_.pop();
      return true;
    }
    return false;
  }

  bool ReceiveNonBlocking(T* t) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return false;
    }
    *t = queue_.front();
    queue_.pop();
    return true;
  }

  bool HasPending() {
    std::unique_lock<std::mutex> lock(mutex_);
    return !queue_.empty();
  }

  bool HasQuitted() {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.empty() && !senders_;
  }

 private:
  friend class SenderImpl<T>;

  void Receive(T t) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      queue_.push(std::move(t));
    }
    notifier_.notify_one();
  }

  void ReleaseSender() {
    senders_--;
    notifier_.notify_one();
  }

  std::mutex mutex_;
  std::queue<T> queue_;
  std::condition_variable notifier_;
  std::atomic<int> senders_{0};
};

template <class T>
Receiver<T> MakeReceiver() {
  return std::make_unique<ReceiverImpl<T>>();
}

}  // namespace ftxui



namespace ftxui {
using ScreenInteractive = App;
}  // namespace ftxui




namespace ftxui {

class Table;
class TableSelection;

/// @brief Table is a utility to draw tables.
///
/// **example**
/// ```cpp
/// auto table = Table({
///  {"X", "Y"},
///  {"-1", "1"},
///  {"+0", "0"},
///  {"+1", "1"},
/// });
///
/// table.SelectAll().Border(LIGHT);
/// table.SelectRow(1).Border(DOUBLE);
/// table.SelectRow(1).SeparatorInternal(LIGHT);
///
/// std::move(table).Render();
/// ```
///
/// @ingroup dom
class FTXUI_EXPORT(DOM) Table {
 public:
  Table();
  explicit Table(const std::vector<std::vector<std::string>>&);
  explicit Table(std::vector<std::vector<Element>>);
  Table(std::initializer_list<std::vector<std::string>> init);
  TableSelection SelectAll();
  TableSelection SelectCell(int column, int row);
  TableSelection SelectRow(int row_index);
  TableSelection SelectRows(int row_min, int row_max);
  TableSelection SelectColumn(int column_index);
  TableSelection SelectColumns(int column_min, int column_max);
  TableSelection SelectRectangle(int column_min,
                                 int column_max,
                                 int row_min,
                                 int row_max);
  Element Render();

 private:
  void Initialize(std::vector<std::vector<Element>>);
  friend TableSelection;
  std::vector<std::vector<Element>> elements_;
  int input_dim_x_ = 0;
  int input_dim_y_ = 0;
  int dim_x_ = 0;
  int dim_y_ = 0;
};

class FTXUI_EXPORT(DOM) TableSelection {
 public:
  // Decorate the whole selection with a decorator.
  void Decorate(const Decorator&);
  void DecorateAlternateRow(const Decorator&, int modulo = 2, int shift = 0);
  void DecorateAlternateColumn(const Decorator&, int modulo = 2, int shift = 0);

  // Decorate only the cells of the selection with a decorator.
  void DecorateCells(const Decorator&);
  void DecorateCellsAlternateColumn(const Decorator&,
                                    int modulo = 2,
                                    int shift = 0);
  void DecorateCellsAlternateRow(const Decorator&,
                                 int modulo = 2,
                                 int shift = 0);

  // Decorate only the border of the selection with a decorator.
  void DecorateBorder(const Decorator&);
  void DecorateBorderLeft(const Decorator&);
  void DecorateBorderRight(const Decorator&);
  void DecorateBorderTop(const Decorator&);
  void DecorateBorderBottom(const Decorator&);

  // Decorate only the separator of the selection with a decorator.
  void DecorateSeparator(const Decorator&);
  void DecorateSeparatorVertical(const Decorator&);
  void DecorateSeparatorHorizontal(const Decorator&);

  // Decorate the border of the selection with a border style and a decorator.
  void Border(BorderStyle border = LIGHT);
  void Border(BorderStyle, const Decorator&);
  void BorderLeft(BorderStyle border = LIGHT);
  void BorderLeft(BorderStyle, const Decorator&);
  void BorderRight(BorderStyle border = LIGHT);
  void BorderRight(BorderStyle, const Decorator&);
  void BorderTop(BorderStyle border = LIGHT);
  void BorderTop(BorderStyle, const Decorator&);
  void BorderBottom(BorderStyle border = LIGHT);
  void BorderBottom(BorderStyle, const Decorator&);

  // Decorate the separator of the selection with a border style and a
  // decorator.
  void Separator(BorderStyle border = LIGHT);
  void Separator(BorderStyle, const Decorator&);
  void SeparatorVertical(BorderStyle border = LIGHT);
  void SeparatorVertical(BorderStyle, const Decorator&);
  void SeparatorHorizontal(BorderStyle border = LIGHT);
  void SeparatorHorizontal(BorderStyle, const Decorator&);

 private:
  friend Table;
  Table* table_;
  int x_min_;
  int x_max_;
  int y_min_;
  int y_max_;
};

}  // namespace ftxui




namespace ftxui {

/// @brief ColorInfo is a structure that contains information about the terminal
/// color palette.
///
/// @ingroup screen
struct FTXUI_EXPORT(SCREEN) ColorInfo {
  const char* name;
  uint8_t index_256;
  uint8_t index_16;
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t hue;
  uint8_t saturation;
  uint8_t value;
};

FTXUI_EXPORT(SCREEN) ColorInfo GetColorInfo(Color::Palette256 index);
FTXUI_EXPORT(SCREEN) ColorInfo GetColorInfo(Color::Palette16 index);

/// @brief Get the color information for the palette256, sorted in 2D.
/// @return A 2D vector of ColorInfo.
/// @ingroup screen
FTXUI_EXPORT(SCREEN) std::vector<std::vector<ColorInfo>> ColorInfoSorted2D();

}  // namespace ftxui




namespace ftxui {
using Pixel = Cell;
}  // namespace ftxui


namespace ftxui {
using Image = Surface;
}  // namespace ftxui



namespace ftxui {

/// Assign a value to a variable, reset its old value when going out of scope.
template <typename T>
class AutoReset {
 public:
  AutoReset(T* variable, T new_value)
      : variable_(variable), previous_value_(std::move(*variable)) {
    *variable_ = std::move(new_value);
  }
  AutoReset(const AutoReset&) = delete;
  AutoReset(AutoReset&&) = delete;
  AutoReset& operator=(const AutoReset&) = delete;
  AutoReset& operator=(AutoReset&&) = delete;
  ~AutoReset() { *variable_ = std::move(previous_value_); }

 private:
  T* variable_;
  T previous_value_;
};

}  // namespace ftxui

#endif // FTXUI_AMALGAMATED_HPP
