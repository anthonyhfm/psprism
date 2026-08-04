#pragma once

#include <psprecomp/runtime.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>

namespace psprism {

struct Configuration {
  std::filesystem::path disc_root;
  std::filesystem::path writable_root;
  std::size_t image_size{};
  std::function<void(psprecomp::State&)> guest_executor;
};

class Runtime final {
public:
  static Runtime& instance();

  void configure(std::uint8_t* memory, std::size_t size, std::uint32_t base,
                 Configuration configuration = {});
  void set_verbose(bool enabled);
  void dispatch(psprecomp::State& state, std::string_view import_name);
  void log(const char* format, std::uint32_t first, std::uint32_t second);
  void prepare_state(psprecomp::State& state);
  void run_host_loop();
  void wait_for_guest_threads();

private:
  Runtime();
  ~Runtime();
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  struct Implementation;
  Implementation* implementation_;
};

} // namespace psprism
