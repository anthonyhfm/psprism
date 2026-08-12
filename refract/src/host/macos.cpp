#include "host.hpp"

#if !defined(__APPLE__)
#error "psprism currently ships only a macOS host backend"
#endif

#include <chrono>
#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <condition_variable>
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

struct AudioChannelOutput {
  std::mutex mutex;
  std::condition_variable available;
  AudioQueueRef queue{};
  std::uint32_t sample_rate{};
  std::size_t queued_buffers{};
  bool callback_stalled{};
};

std::array<AudioChannelOutput, 8>& audio_outputs() {
  static std::array<AudioChannelOutput, 8> value;
  return value;
}

void audio_buffer_finished(void* context, AudioQueueRef queue,
                           AudioQueueBufferRef buffer) {
  AudioQueueFreeBuffer(queue, buffer);
  auto& output = *static_cast<AudioChannelOutput*>(context);
  {
    std::lock_guard lock(output.mutex);
    if (output.queued_buffers != 0U) --output.queued_buffers;
    output.callback_stalled = false;
  }
  output.available.notify_one();
}

bool configure_audio_queue(AudioChannelOutput& output,
                           std::uint32_t sample_rate) {
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
  if (AudioQueueNewOutput(&format, audio_buffer_finished, &output, nullptr,
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
                  std::uint32_t frame_count, std::uint32_t channel,
                  bool blocking, std::uint32_t sample_rate) {
  if (interleaved_stereo == nullptr || frame_count == 0U ||
      sample_rate == 0U || channel >= audio_outputs().size())
    return false;
  const auto byte_count = static_cast<std::size_t>(frame_count) * 2U *
                          sizeof(std::int16_t);
  if (byte_count > std::numeric_limits<UInt32>::max()) return false;

  auto& output = audio_outputs()[channel];
  std::unique_lock lock(output.mutex);
  if (!configure_audio_queue(output, sample_rate)) return false;
  constexpr std::size_t maximum_queued_buffers = 2U;
  if (blocking) {
    // AudioQueue normally calls audio_buffer_finished as soon as a queued
    // buffer has played.  A suspended/reconfigured host device can, however,
    // stop delivering callbacks.  PSP games frequently hold their own sound
    // mutex while making a blocking output call, so waiting forever here can
    // freeze the entire game rather than merely dropping audio.
    if (output.callback_stalled) return false;
    // Large, valid PSP buffers can represent close to a second of audio.  A
    // cold AudioQueue has occasionally failed to consume those initial
    // buffers, and scaling this timeout without a ceiling makes a game's
    // producer thread advance only once every several seconds.  Bound that
    // failure mode while retaining four buffer durations for normal chunks.
    const auto callback_timeout = std::chrono::microseconds(
        audio_callback_timeout_microseconds(frame_count, sample_rate));
    if (!output.available.wait_for(lock, callback_timeout, [&output] {
          return output.queued_buffers < maximum_queued_buffers;
        })) {
      output.callback_stalled = true;
      return false;
    }
  } else if (output.queued_buffers >= maximum_queued_buffers) {
    return false;
  }
  AudioQueueBufferRef buffer{};
  if (AudioQueueAllocateBuffer(output.queue, static_cast<UInt32>(byte_count),
                               &buffer) != noErr)
    return false;
  std::memcpy(buffer->mAudioData, interleaved_stereo, byte_count);
  buffer->mAudioDataByteSize = static_cast<UInt32>(byte_count);
  ++output.queued_buffers;
  if (AudioQueueEnqueueBuffer(output.queue, buffer, 0U, nullptr) != noErr) {
    --output.queued_buffers;
    AudioQueueFreeBuffer(output.queue, buffer);
    return false;
  }
  return true;
}

} // namespace refract::host
