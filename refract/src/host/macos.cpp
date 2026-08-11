#include "host.hpp"

#if !defined(__APPLE__)
#error "psprism currently ships only a macOS host backend"
#endif

#include <chrono>
#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>

namespace refract::host {

std::uint64_t monotonic_microseconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

std::uint64_t unix_seconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

void sleep_microseconds(std::uint32_t duration) {
  std::this_thread::sleep_for(std::chrono::microseconds(duration));
}

namespace {

struct AudioOutput {
  std::mutex mutex;
  AudioQueueRef queue{};
  std::uint32_t sample_rate{};
};

AudioOutput& audio_output() {
  static AudioOutput value;
  return value;
}

void audio_buffer_finished(void*, AudioQueueRef queue,
                           AudioQueueBufferRef buffer) {
  AudioQueueFreeBuffer(queue, buffer);
}

bool configure_audio_queue(AudioOutput& output, std::uint32_t sample_rate) {
  if (output.queue != nullptr && output.sample_rate == sample_rate) return true;
  if (output.queue != nullptr) {
    AudioQueueStop(output.queue, true);
    AudioQueueDispose(output.queue, true);
    output.queue = nullptr;
  }

  AudioStreamBasicDescription format{};
  format.mSampleRate = sample_rate;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger |
                        kLinearPCMFormatFlagIsPacked |
                        kAudioFormatFlagsNativeEndian;
  format.mBytesPerPacket = 2U * sizeof(std::int16_t);
  format.mFramesPerPacket = 1U;
  format.mBytesPerFrame = format.mBytesPerPacket;
  format.mChannelsPerFrame = 2U;
  format.mBitsPerChannel = 16U;
  if (AudioQueueNewOutput(&format, audio_buffer_finished, nullptr, nullptr,
                          nullptr, 0U, &output.queue) != noErr) {
    output.queue = nullptr;
    return false;
  }
  if (AudioQueueStart(output.queue, nullptr) != noErr) {
    AudioQueueDispose(output.queue, true);
    output.queue = nullptr;
    return false;
  }
  output.sample_rate = sample_rate;
  return true;
}

} // namespace

bool submit_audio(const std::int16_t* interleaved_stereo,
                  std::uint32_t frame_count, std::uint32_t sample_rate) {
  if (interleaved_stereo == nullptr || frame_count == 0U ||
      sample_rate == 0U)
    return false;
  const auto byte_count = static_cast<std::size_t>(frame_count) * 2U *
                          sizeof(std::int16_t);
  if (byte_count > std::numeric_limits<UInt32>::max()) return false;

  auto& output = audio_output();
  std::lock_guard lock(output.mutex);
  if (!configure_audio_queue(output, sample_rate)) return false;
  AudioQueueBufferRef buffer{};
  if (AudioQueueAllocateBuffer(output.queue, static_cast<UInt32>(byte_count),
                               &buffer) != noErr)
    return false;
  std::memcpy(buffer->mAudioData, interleaved_stereo, byte_count);
  buffer->mAudioDataByteSize = static_cast<UInt32>(byte_count);
  if (AudioQueueEnqueueBuffer(output.queue, buffer, 0U, nullptr) != noErr) {
    AudioQueueFreeBuffer(output.queue, buffer);
    return false;
  }
  return true;
}

} // namespace refract::host
