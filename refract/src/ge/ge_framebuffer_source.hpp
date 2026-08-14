#pragma once

#include "ge_draw_packet.hpp"

#include <cstdint>
#include <mutex>
#include <span>
#include <unordered_map>

namespace refract::ge {

// PSP framebuffers can be written either by GE commands or directly by the
// CPU (notably sceMpegAvcDecode).  Native render targets do not share storage
// with guest VRAM, so presentation must remember which side wrote an address
// most recently.  An unchanged CPU snapshot does not supersede a later GE
// write merely because sceDisplaySetFrameBuf selected the buffer again.
class FramebufferSourceTracker {
public:
  bool record_cpu_frame(std::uint32_t address,
                        std::span<const std::uint8_t> pixels) {
    const auto hash = content_hash(pixels);
    std::lock_guard lock(mutex_);
    auto& entry = entries_[address];
    if (entry.has_cpu_hash && entry.cpu_hash == hash) return false;
    entry.cpu_hash = hash;
    entry.has_cpu_hash = true;
    entry.cpu_is_latest = true;
    return true;
  }

  void record_ge_write(std::uint32_t address) {
    std::lock_guard lock(mutex_);
    entries_[address].cpu_is_latest = false;
  }

  bool cpu_is_latest(std::uint32_t address) const {
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(address);
    return found != entries_.end() && found->second.cpu_is_latest;
  }

  void reset() {
    std::lock_guard lock(mutex_);
    entries_.clear();
  }

private:
  struct Entry {
    std::uint64_t cpu_hash{};
    bool has_cpu_hash{};
    bool cpu_is_latest{};
  };

  mutable std::mutex mutex_;
  std::unordered_map<std::uint32_t, Entry> entries_;
};

} // namespace refract::ge
