#include <psprism/psprism.hpp>

#include "host/host.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>

namespace psprism {
namespace {

constexpr std::uint32_t unimplemented = 0x8002013aU;
constexpr std::uint32_t io_error = 0x80010005U;

template <typename T>
T *guest_pointer(psprecomp::State &state, std::uint32_t address) {
  if (!psprecomp::address_ok(state, address, sizeof(T))) {
    return nullptr;
  }
  return reinterpret_cast<T *>(state.memory + address - state.memory_base);
}

char *guest_string(psprecomp::State &state, std::uint32_t address) {
  if (address < state.memory_base) {
    return nullptr;
  }
  const auto offset = static_cast<std::size_t>(address - state.memory_base);
  if (offset >= state.memory_size ||
      std::memchr(state.memory + offset, 0, state.memory_size - offset) ==
          nullptr) {
    return nullptr;
  }
  return reinterpret_cast<char *>(state.memory + offset);
}

bool is_one_of(std::string_view value,
               std::initializer_list<std::string_view> choices) {
  for (const auto choice : choices) {
    if (value == choice) {
      return true;
    }
  }
  return false;
}

int host_open_flags(std::uint32_t flags) {
  int result = O_RDONLY;
  if ((flags & 3U) == 2U)
    result = O_WRONLY;
  if ((flags & 3U) == 3U)
    result = O_RDWR;
  if ((flags & 0x0100U) != 0)
    result |= O_APPEND;
  if ((flags & 0x0200U) != 0)
    result |= O_CREAT;
  if ((flags & 0x0400U) != 0)
    result |= O_TRUNC;
  return result;
}

} // namespace

struct Runtime::Implementation {
  std::uint8_t *memory{};
  std::size_t memory_size{};
  std::uint32_t memory_base{};
  Configuration configuration;
  std::unordered_set<std::string> warned;
  std::unordered_map<int, int> files;
  int next_file{3};

  std::filesystem::path resolve_path(std::string_view psp_path) const {
    const auto suffix = [](std::string_view value, std::string_view prefix) {
      value.remove_prefix(prefix.size());
      while (!value.empty() && value.front() == '/')
        value.remove_prefix(1);
      return value;
    };
    if (psp_path.starts_with("disc0:") || psp_path.starts_with("umd0:")) {
      const auto prefix = psp_path.starts_with("disc0:") ? "disc0:" : "umd0:";
      return configuration.disc_root / suffix(psp_path, prefix);
    }
    if (psp_path.starts_with("ms0:")) {
      return configuration.writable_root / suffix(psp_path, "ms0:");
    }
    return configuration.disc_root / suffix(psp_path, "");
  }

  int descriptor(int psp_descriptor) const {
    if (psp_descriptor >= 0 && psp_descriptor <= 2)
      return psp_descriptor;
    const auto found = files.find(psp_descriptor);
    return found == files.end() ? -1 : found->second;
  }
};

Runtime &Runtime::instance() {
  static Runtime runtime;
  return runtime;
}

Runtime::Runtime() : implementation_(new Implementation) {}

Runtime::~Runtime() {
  for (const auto &[psp_descriptor, host_descriptor] : implementation_->files) {
    static_cast<void>(psp_descriptor);
    ::close(host_descriptor);
  }
  delete implementation_;
}

void Runtime::configure(std::uint8_t *memory, std::size_t size,
                        std::uint32_t base, Configuration configuration) {
  implementation_->memory = memory;
  implementation_->memory_size = size;
  implementation_->memory_base = base;
  if (configuration.disc_root.empty()) {
    if (const auto *value = std::getenv("PSPRISM_DISC_ROOT")) {
      configuration.disc_root = value;
    } else {
      configuration.disc_root = std::filesystem::current_path() / "disc";
    }
  }
  if (configuration.writable_root.empty()) {
    if (const auto *value = std::getenv("PSPRISM_WRITABLE_ROOT")) {
      configuration.writable_root = value;
    } else {
      configuration.writable_root =
          std::filesystem::current_path() / ".psprism" / "ms0";
    }
  }
  std::filesystem::create_directories(configuration.writable_root);
  implementation_->configuration = std::move(configuration);
  std::fprintf(stderr, "[psprism:macos] disc=%s writable=%s\n",
               implementation_->configuration.disc_root.c_str(),
               implementation_->configuration.writable_root.c_str());
}

void Runtime::log(const char *format, std::uint32_t first,
                  std::uint32_t second) {
  std::fprintf(stderr, format, first, second);
}

void Runtime::dispatch(psprecomp::State &state, std::string_view name) {
  state.gpr[2] = unimplemented;

  if (is_one_of(name,
                {"sceKernelGetModuleId", "sceKernelSetCompilerVersion",
                 "sceKernelSetCompiledSdkVersion", "sceKernelPowerTick",
                 "sceKernelDcacheWritebackAll", "sceCtrlSetSamplingMode",
                 "sceCtrlSetSamplingCycle", "sceCtrlSetIdleCancelThreshold",
                 "sceDisplaySetMode", "sceMpegAvcDecodeFlush"})) {
    state.gpr[2] = 0;
    return;
  }
  if (is_one_of(name, {"sceKernelExitGame", "sceKernelExitThread",
                       "sceKernelSleepThreadCB"})) {
    state.gpr[2] = 0;
    state.stop_reason = psprecomp::StopReason::returned;
    return;
  }
  if (name == "sceKernelPrintf") {
    if (const auto *format = guest_string(state, state.gpr[4])) {
      std::fprintf(stderr, "[guest] %s", format);
    }
    state.gpr[2] = 0;
    return;
  }
  if (name == "sceKernelGetSystemTimeWide") {
    const auto value = host::monotonic_microseconds();
    state.gpr[2] = static_cast<std::uint32_t>(value);
    state.gpr[3] = static_cast<std::uint32_t>(value >> 32U);
    return;
  }
  if (name == "sceKernelLibcClock") {
    state.gpr[2] = static_cast<std::uint32_t>(host::monotonic_microseconds());
    return;
  }
  if (name == "sceKernelLibcTime") {
    const auto seconds = static_cast<std::uint32_t>(host::unix_seconds());
    if (auto *destination = guest_pointer<std::uint32_t>(state, state.gpr[4])) {
      *destination = seconds;
    }
    state.gpr[2] = seconds;
    return;
  }
  if (is_one_of(name, {"sceKernelDelayThread", "sceKernelDelayThreadCB"})) {
    host::sleep_microseconds(state.gpr[4]);
    state.gpr[2] = 0;
    return;
  }
  if (name == "sceDisplayGetFramePerSec") {
    state.gpr[2] = 0x41efc71cU; // 59.940060f
    state.fpr[0] = state.gpr[2];
    return;
  }
  if (name == "sceDisplayGetVcount") {
    state.gpr[2] =
        static_cast<std::uint32_t>(host::monotonic_microseconds() / 16683U);
    return;
  }
  if (name == "sceDisplayWaitVblankStart") {
    host::sleep_microseconds(16683U);
    state.gpr[2] = 0;
    return;
  }
  if (is_one_of(name, {"sceUmdCheckMedium", "sceUmdWaitDriveStatCB",
                       "sceUmdCancelWaitDriveStat", "sceUmdWaitDriveStat",
                       "sceUmdActivate"})) {
    state.gpr[2] = name == "sceUmdCheckMedium" ? 1U : 0U;
    return;
  }
  if (name == "sceUmdGetDriveStat") {
    state.gpr[2] = 0x32U; // present, ready, readable
    return;
  }
  if (is_one_of(name,
                {"sceCtrlReadBufferPositive", "sceCtrlPeekBufferPositive"})) {
    constexpr std::size_t pad_size = 16;
    const auto count = state.gpr[5];
    if (!psprecomp::address_ok(state, state.gpr[4], pad_size * count))
      return;
    auto *output = state.memory + state.gpr[4] - state.memory_base;
    std::memset(output, 0, pad_size * count);
    for (std::uint32_t index = 0; index < count; ++index) {
      const auto timestamp =
          static_cast<std::uint32_t>(host::monotonic_microseconds());
      std::memcpy(output + index * pad_size, &timestamp, sizeof(timestamp));
      output[index * pad_size + 8] = 128;
      output[index * pad_size + 9] = 128;
    }
    state.gpr[2] = count;
    return;
  }
  if (is_one_of(name, {"sceIoOpen", "sceIoOpenAsync"})) {
    const auto *path = guest_string(state, state.gpr[4]);
    if (path == nullptr)
      return;
    const auto resolved = implementation_->resolve_path(path);
    if ((state.gpr[5] & 0x0200U) != 0) {
      std::error_code ignored;
      std::filesystem::create_directories(resolved.parent_path(), ignored);
    }
    const auto descriptor =
        ::open(resolved.c_str(), host_open_flags(state.gpr[5]),
               static_cast<mode_t>(state.gpr[6]));
    if (descriptor < 0) {
      state.gpr[2] = io_error;
    } else {
      const auto psp_descriptor = implementation_->next_file++;
      implementation_->files.emplace(psp_descriptor, descriptor);
      state.gpr[2] = static_cast<std::uint32_t>(psp_descriptor);
    }
    return;
  }
  if (name == "sceIoClose") {
    const auto found =
        implementation_->files.find(static_cast<int>(state.gpr[4]));
    if (found == implementation_->files.end())
      return;
    state.gpr[2] = ::close(found->second) == 0 ? 0U : io_error;
    implementation_->files.erase(found);
    return;
  }
  if (is_one_of(name, {"sceIoRead", "sceIoReadAsync", "sceIoWrite"})) {
    const auto descriptor =
        implementation_->descriptor(static_cast<int>(state.gpr[4]));
    const auto size = static_cast<std::size_t>(state.gpr[6]);
    if (descriptor < 0 || !psprecomp::address_ok(state, state.gpr[5], size))
      return;
    auto *buffer = state.memory + state.gpr[5] - state.memory_base;
    const auto result = name == "sceIoWrite" ? ::write(descriptor, buffer, size)
                                             : ::read(descriptor, buffer, size);
    state.gpr[2] = result < 0 ? io_error : static_cast<std::uint32_t>(result);
    return;
  }
  if (is_one_of(name, {"sceIoLseek", "sceIoLseekAsync"})) {
    const auto descriptor =
        implementation_->descriptor(static_cast<int>(state.gpr[4]));
    if (descriptor < 0)
      return;
    const auto bits = static_cast<std::uint64_t>(state.gpr[5]) |
                      static_cast<std::uint64_t>(state.gpr[6]) << 32U;
    const auto result = ::lseek(descriptor, static_cast<std::int64_t>(bits),
                                static_cast<int>(state.gpr[7]));
    if (result < 0) {
      state.gpr[2] = io_error;
      state.gpr[3] = 0xffffffffU;
    } else {
      state.gpr[2] = static_cast<std::uint32_t>(result);
      state.gpr[3] =
          static_cast<std::uint32_t>(static_cast<std::uint64_t>(result) >> 32U);
    }
    return;
  }

  if (implementation_->warned.emplace(name).second) {
    std::fprintf(stderr, "[psprism:macos] unimplemented: %.*s\n",
                 static_cast<int>(name.size()), name.data());
  }
}

} // namespace psprism
