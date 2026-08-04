#include "tui.hpp"

#include "project.hpp"

#include <ftxui.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace psprecomp::tui {
namespace {

using namespace ftxui;

const auto kCyan = Color::CyanLight;
const auto kViolet = Color::RGB(183, 115, 255);
const auto kLime = Color::RGB(169, 255, 112);
const auto kBackground = Color::RGB(8, 10, 22);

bool colors_enabled() {
  return std::getenv("NO_COLOR") == nullptr;
}

Element accent(Element element, Color tint) {
  return colors_enabled() ? std::move(element) | ftxui::color(tint)
                          : std::move(element);
}

std::string short_path(const std::filesystem::path& path, std::size_t width = 58) {
  auto text = path.string();
  if (text.size() <= width) {
    return text;
  }
  return "..." + text.substr(text.size() - width + 3U);
}

class Wizard {
 public:
  Wizard(InitOptions options, Context context)
      : options_(std::move(options)), context_(std::move(context)),
        input_(options_.input.string()), display_name_(options_.display_name),
        project_name_(options_.project_name), output_(options_.output.string()),
        code_map_(options_.code_map ? options_.code_map->string() : ""),
        extract_disc_(options_.extract_disc) {
    browser_directory_ = std::filesystem::current_path();
    refresh_browser();
  }

  ~Wizard() {
    cancel_requested_.store(true);
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  int run() {
    app_ = std::make_unique<App>(App::Fullscreen());
    app_->ForceHandleCtrlC(true);
    app_->TrackMouse(true);
    build_components();
    if (!input_.empty()) {
      inspect_input();
    }
    app_->Loop(root_);
    if (worker_.joinable()) {
      worker_.join();
    }
    return exit_code_;
  }

 private:
  enum Page { input_page, configure_page, review_page, export_page, done_page, error_page };

  void build_components() {
    InputOption compact;
    compact.multiline = false;
    compact.transform = [](InputState state) {
      auto element = state.element | border;
      if (!colors_enabled()) {
        return element;
      }
      return element | (state.focused ? color(kCyan) : color(Color::GrayLight));
    };

    input_field_ = Input(&input_, "Path to a PSP ISO, ELF or PRX", compact);
    display_field_ = Input(&display_name_, "Display name", compact);
    project_field_ = Input(&project_name_, "Build-safe project name", compact);
    output_field_ = Input(&output_, "Output directory", compact);
    map_field_ = Input(&code_map_, "Optional Ghidra code map", compact);

    browser_menu_ = Menu(&browser_entries_, &browser_selected_);
    input_actions_ = Container::Vertical({
        Button("Inspect input", [&] { inspect_input(); }, ButtonOption::Border()),
        Button("Quit", [&] { quit(); }, ButtonOption::Simple()),
    });
    input_view_ = Container::Vertical({input_field_, browser_menu_, input_actions_});
    extract_disc_checkbox_ = Checkbox("Extract full disc filesystem", &extract_disc_);
    configure_actions_ = Container::Vertical({
        Button("Continue to review", [&] { prepare_review(); }, ButtonOption::Border()),
        Button("Back", [&] { page_ = input_page; }, ButtonOption::Simple()),
    });
    configure_view_ = Container::Vertical({
        display_field_, project_field_, output_field_, map_field_, extract_disc_checkbox_,
        configure_actions_,
    });
    review_actions_ = Container::Vertical({
        Button("Start export", [&] { start_export(); }, ButtonOption::Border()),
        Button("Back to edit", [&] { page_ = configure_page; }, ButtonOption::Simple()),
    });
    review_view_ = review_actions_;
    export_view_ = Renderer([&] { return export_body(); });
    done_view_ = Container::Vertical({
        Button("Done", [&] { quit(); }, ButtonOption::Border()),
    });
    error_view_ = Container::Vertical({
        Button("Back to configuration", [&] { page_ = configure_page; }, ButtonOption::Border()),
        Button("Quit", [&] { quit(); }, ButtonOption::Simple()),
    });

    pages_ = Container::Tab({input_view_, configure_view_, review_view_, export_view_,
                             done_view_, error_view_},
                            &page_);
    root_ = CatchEvent(Renderer(pages_, [&] { return render(); }),
                       [&](Event event) { return handle_event(event); });
  }

  void refresh_browser() {
    browser_entries_.clear();
    browser_paths_.clear();
    const auto parent = browser_directory_.parent_path();
    if (parent != browser_directory_) {
      browser_entries_.push_back("[..] " + short_path(parent));
      browser_paths_.push_back(parent);
    }
    std::error_code error;
    std::vector<std::filesystem::directory_entry> entries;
    for (std::filesystem::directory_iterator it(browser_directory_, error), end;
         !error && it != end; it.increment(error)) {
      entries.push_back(*it);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
      if (left.is_directory() != right.is_directory()) {
        return left.is_directory();
      }
      return left.path().filename().string() < right.path().filename().string();
    });
    for (const auto& entry : entries) {
      const auto directory = entry.is_directory(error);
      browser_entries_.push_back(std::string(directory ? "[dir] " : "      ") +
                                 entry.path().filename().string());
      browser_paths_.push_back(entry.path());
    }
    if (browser_entries_.empty()) {
      browser_entries_.push_back("(empty directory)");
    }
    browser_selected_ = 0;
  }

  void inspect_input() {
    try {
      if (input_.empty()) {
        throw std::runtime_error("choose a PSP ISO, ELF or PRX first");
      }
      const auto input = std::filesystem::absolute(input_).lexically_normal();
      source_ = inspect_source(input);
      input_ = input.string();
      if (display_name_.empty()) {
        display_name_ = source_->suggested_display_name.empty()
                            ? (source_->disc_id.empty() ? input.stem().string()
                                                         : source_->disc_id)
                            : source_->suggested_display_name;
      }
      if (display_name_.empty()) {
        display_name_ = "PSP Recompiled";
      }
      if (project_name_.empty()) {
        project_name_ = project_slug(display_name_);
      }
      if (output_.empty()) {
        output_ = (std::filesystem::current_path() / project_name_).string();
      }
      page_ = configure_page;
    } catch (const std::exception& error) {
      error_ = error.what();
      page_ = error_page;
    }
  }

  void prepare_review() {
    try {
      if (!source_) {
        throw std::runtime_error("inspect the input before continuing");
      }
      if (display_name_.empty()) {
        throw std::runtime_error("display name cannot be empty");
      }
      project_name_ = project_slug(project_name_.empty() ? display_name_ : project_name_);
      if (output_.empty()) {
        throw std::runtime_error("output directory cannot be empty");
      }
      const auto output = std::filesystem::absolute(output_).lexically_normal();
      if (std::filesystem::exists(output)) {
        throw std::runtime_error("output already exists: " + output.string());
      }
      if (!code_map_.empty() && !std::filesystem::is_regular_file(code_map_)) {
        throw std::runtime_error("code map does not exist: " + code_map_);
      }
      output_ = output.string();
      page_ = review_page;
    } catch (const std::exception& error) {
      error_ = error.what();
      page_ = error_page;
    }
  }

  void start_export() {
    if (worker_.joinable()) {
      return;
    }
    page_ = export_page;
    cancel_requested_.store(false);
    log_lines_.clear();
    active_stage_ = "Preparing export";
    worker_ = std::thread([this] {
      try {
        ExportConfig config;
        config.input = input_;
        config.output_directory = output_;
        config.runtime_include_directory = context_.runtime_include_directory;
        config.psprism_directory = context_.psprism_directory;
        if (!code_map_.empty()) {
          config.code_map = std::filesystem::absolute(code_map_).lexically_normal();
        }
        config.display_name = display_name_;
        config.project_name = project_name_;
        config.disc_id = source_->disc_id;
        config.extract_disc = extract_disc_;
        config.progress = [this](std::string_view message) {
          std::scoped_lock lock(state_mutex_);
          active_stage_ = std::string(message);
          log_lines_.emplace_back(message);
          if (app_) {
            app_->PostEvent(Event::Custom);
          }
        };
        config.is_cancel_requested = [this] { return cancel_requested_.load(); };
        const auto summary = export_codebase(config);
        std::scoped_lock lock(state_mutex_);
        summary_ = summary;
        finished_ = true;
      } catch (const ExportCancelled&) {
        std::scoped_lock lock(state_mutex_);
        cancelled_ = true;
        finished_ = true;
      } catch (const std::exception& error) {
        std::scoped_lock lock(state_mutex_);
        error_ = error.what();
        failed_ = true;
        finished_ = true;
      }
      if (app_) {
        app_->PostEvent(Event::Custom);
      }
    });
  }

  Element header() const {
    const auto logo = text("  ____  ____  ____  ____  _____ ____ ___  __  __ ____  ") | bold;
    const auto name = text(" PSPRECOMP  |  Static recompilation workspace") | dim;
    return vbox({accent(logo, kCyan), accent(name, kViolet), separator()});
  }

  Element rail() const {
    const std::vector<std::string> steps{"01 INPUT", "02 CONFIGURE", "03 REVIEW", "04 EXPORT", "05 DONE"};
    Elements items;
    for (std::size_t index = 0; index < steps.size(); ++index) {
      const bool active = static_cast<int>(index) == std::min(page_, 4);
      const bool complete = static_cast<int>(index) < page_;
      auto item = text(std::string(complete ? " ✓ " : active ? " › " : " · ") + steps[index]);
      items.push_back(accent(item | (active ? bold : dim), active ? kLime : Color::GrayLight));
    }
    return vbox(std::move(items)) | border | size(WIDTH, EQUAL, 18);
  }

  Element input_body() const {
    return vbox({
        accent(text("Bring your legally obtained PSP game."), kCyan),
        text("Enter a path or pick one from the folder below.") | dim,
        separator(),
        text("PSP ISO, ELF or PRX"), input_field_->Render(),
        text("Browser: " + short_path(browser_directory_)) | dim,
        browser_menu_->Render() | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 10),
        separator(),
        hbox({text("Enter a folder with Right Arrow or Enter.  ") | dim,
              text("Esc quits.") | dim}),
        input_actions_->Render(),
    });
  }

  Element configure_body() const {
    Elements fields{
        text("Display name"), display_field_->Render(),
        text("Project name"), project_field_->Render(),
        text("Output directory"), output_field_->Render(),
        text("Ghidra code map"), map_field_->Render(),
    };
    if (source_ && source_->kind == InputKind::iso) {
      fields.push_back(separator());
      fields.push_back(extract_disc_checkbox_->Render());
    }
    fields.push_back(separator());
    fields.push_back(configure_actions_->Render());
    return vbox(std::move(fields));
  }

  Element review_body() const {
    const auto kind = source_ && source_->kind == InputKind::iso ? "ISO image" : "PSP executable";
    return vbox({
        accent(text("Ready to create your codebase"), kLime) | bold,
        separator(),
        text("Input       " + input_),
        text("Type        " + std::string(kind)),
        text("Executable  " + (source_ ? source_->executable_path : "")),
        text("Name        " + display_name_),
        text("Project     " + project_name_),
        text("Output      " + output_),
        text("Code map    " + (code_map_.empty() ? "none" : code_map_)),
        text("Disc files  " + std::string(extract_disc_ ? "extract all" : "executable only")),
        separator(),
        text("The output directory will be published only after a successful export.") | dim,
        review_actions_->Render(),
    });
  }

  Element export_body() const {
    std::scoped_lock lock(state_mutex_);
    if (finished_) {
      if (cancelled_) {
        return vbox({accent(text("Export cancelled safely."), kLime) | bold,
                     text("The temporary project directory was removed."),
                     text("Press Enter to return to configuration.") | dim});
      }
      if (failed_) {
        return vbox({accent(text("Export failed"), Color::RedLight) | bold,
                     paragraph(error_), text("Press Enter to return to configuration.") | dim});
      }
      return vbox({accent(text("Codebase created"), kLime) | bold,
                   text(summary_ ? summary_->output_directory.string() : output_),
                   text("Next: cd into the project and run  make psp-run") | dim,
                   text("Press Enter to finish.") | dim});
    }
    Elements log;
    for (const auto& line : log_lines_) {
      log.push_back(text("› " + line) | dim);
    }
    if (log.empty()) {
      log.push_back(text("› Preparing workspace") | dim);
    }
    return vbox({
        accent(text("◌  " + active_stage_), kCyan) | bold,
        gauge(0.55) | color(kViolet),
        separator(),
        vbox(std::move(log)) | vscroll_indicator | frame | flex,
        separator(),
        text("Esc or Ctrl+C requests a safe cancellation.") | dim,
    });
  }

  Element done_body() const {
    return vbox({accent(text("Your recompilation workspace is ready."), kLime) | bold,
                 text(summary_ ? summary_->output_directory.string() : output_),
                 separator(), text("cd \"" + output_ + "\""),
                 accent(text("make psp-run"), kCyan), done_view_->Render() | nothing});
  }

  Element error_body() const {
    return vbox({accent(text("Something needs attention"), Color::RedLight) | bold,
                 separator(), paragraph(error_), separator(), error_view_->Render() | nothing});
  }

  Element render() const {
    Element body;
    switch (page_) {
      case input_page: body = input_body(); break;
      case configure_page: body = configure_body(); break;
      case review_page: body = review_body(); break;
      case export_page: body = export_body(); break;
      case done_page: body = done_body(); break;
      case error_page: body = error_body(); break;
      default: body = text("Unknown wizard state"); break;
    }
    auto layout = vbox({header(), hbox({rail(), filler(), body | flex | border}),
                        separator(), text("TAB focus  •  ENTER select  •  ? help  •  ESC back") | dim});
    return colors_enabled() ? layout | bgcolor(kBackground) | flex : layout | flex;
  }

  bool handle_event(Event event) {
    if (event == Event::Custom) {
      std::scoped_lock lock(state_mutex_);
      if (finished_) {
        if (failed_) page_ = error_page;
        else if (!cancelled_) page_ = done_page;
      }
      return true;
    }
    if (event == Event::Character('?') || event == Event::F1) {
      error_ = "Keyboard: Tab changes focus, Enter activates an item, arrows navigate the browser, Esc goes back. During export Esc or Ctrl+C requests a safe cancellation.";
      page_ = error_page;
      return true;
    }
    if (page_ == input_page && browser_menu_->Focused() &&
        (event == Event::Return || event == Event::ArrowRight) &&
        browser_selected_ >= 0 && static_cast<std::size_t>(browser_selected_) < browser_paths_.size()) {
      const auto selected = browser_paths_[static_cast<std::size_t>(browser_selected_)];
      std::error_code error;
      if (std::filesystem::is_directory(selected, error)) {
        browser_directory_ = selected;
        refresh_browser();
      } else if (!error) {
        input_ = selected.string();
      }
      return true;
    }
    if (page_ == export_page && (event == Event::Escape || event == Event::CtrlC)) {
      cancel_requested_.store(true);
      return true;
    }
    if ((page_ == export_page && finished_) && event == Event::Return) {
      std::scoped_lock lock(state_mutex_);
      page_ = cancelled_ || failed_ ? configure_page : done_page;
      return true;
    }
    if (event == Event::Escape) {
      if (page_ == input_page) {
        quit();
      } else if (page_ == configure_page) {
        page_ = input_page;
      } else if (page_ == review_page) {
        page_ = configure_page;
      }
      return true;
    }
    if ((event == Event::Character('q') || event == Event::Character('Q')) && page_ != export_page) {
      quit();
      return true;
    }
    return false;
  }

  void quit() {
    exit_code_ = 0;
    app_->Exit();
  }

  InitOptions options_;
  Context context_;
  std::string input_;
  std::string display_name_;
  std::string project_name_;
  std::string output_;
  std::string code_map_;
  bool extract_disc_{};
  std::optional<SourceInfo> source_;
  std::filesystem::path browser_directory_;
  std::vector<std::string> browser_entries_;
  std::vector<std::filesystem::path> browser_paths_;
  int browser_selected_{};
  int page_{input_page};
  std::string error_;
  std::string active_stage_;
  std::vector<std::string> log_lines_;
  std::optional<ExportSummary> summary_;
  std::atomic_bool cancel_requested_{false};
  mutable std::mutex state_mutex_;
  std::thread worker_;
  bool finished_{};
  bool cancelled_{};
  bool failed_{};
  int exit_code_{};
  std::unique_ptr<App> app_;
  Component input_field_;
  Component display_field_;
  Component project_field_;
  Component output_field_;
  Component map_field_;
  Component browser_menu_;
  Component input_actions_;
  Component configure_actions_;
  Component review_actions_;
  Component extract_disc_checkbox_;
  Component input_view_;
  Component configure_view_;
  Component review_view_;
  Component export_view_;
  Component done_view_;
  Component error_view_;
  Component pages_;
  Component root_;
};

}  // namespace

bool supported_terminal() {
#if defined(_WIN32)
  return true;
#else
  const char* term = std::getenv("TERM");
  return term != nullptr && std::string_view(term) != "dumb";
#endif
}

int run_wizard(InitOptions options, const Context& context) {
  return Wizard(std::move(options), context).run();
}

}  // namespace psprecomp::tui
