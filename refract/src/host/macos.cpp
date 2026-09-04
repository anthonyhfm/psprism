#include "host.hpp"
#include "audio_engine.hpp"

#if !defined(__APPLE__)
#error "psprism currently ships only a macOS host backend"
#endif

#include <chrono>
#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <atomic>
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

constexpr std::uint32_t host_audio_sample_rate = 44100U;
constexpr std::uint32_t host_audio_buffer_frames = 512U;
constexpr std::size_t host_audio_buffer_count = 3U;

struct AudioOutput {
  ~AudioOutput() {
    std::lock_guard lock(configuration_mutex);
    if (queue != nullptr) {
      AudioQueueStop(queue, true);
      AudioQueueDispose(queue, true);
    }
  }

  std::mutex configuration_mutex;
  AudioQueueRef queue{};
  std::array<AudioQueueBufferRef, host_audio_buffer_count> buffers{};
  AudioEngine engine;
  std::atomic<bool> device_failed{};
};

AudioOutput& audio_output() {
  static AudioOutput value;
  return value;
}

void audio_buffer_finished(void* context, AudioQueueRef queue,
                           AudioQueueBufferRef buffer) {
  auto& output = *static_cast<AudioOutput*>(context);
  auto* samples = static_cast<std::int16_t*>(buffer->mAudioData);
  output.engine.consume(samples, host_audio_buffer_frames);
  buffer->mAudioDataByteSize = host_audio_buffer_frames * 2U *
                               sizeof(std::int16_t);
  if (AudioQueueEnqueueBuffer(queue, buffer, 0U, nullptr) != noErr)
    output.device_failed.store(true, std::memory_order_relaxed);
}

bool configure_audio_queue(AudioOutput& output) {
  std::lock_guard lock(output.configuration_mutex);
  if (output.queue != nullptr &&
      !output.device_failed.load(std::memory_order_relaxed))
    return true;
  if (output.queue != nullptr) {
    AudioQueueStop(output.queue, true);
    AudioQueueDispose(output.queue, true);
    output.queue = nullptr;
    output.buffers.fill(nullptr);
    output.engine.device_reset();
  }

  AudioStreamBasicDescription format{};
  format.mSampleRate = host_audio_sample_rate;
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
  constexpr auto byte_count = host_audio_buffer_frames * 2U *
                              sizeof(std::int16_t);
  for (auto& buffer : output.buffers) {
    if (AudioQueueAllocateBuffer(output.queue, byte_count, &buffer) != noErr) {
      AudioQueueDispose(output.queue, true);
      output.queue = nullptr;
      output.buffers.fill(nullptr);
      return false;
    }
    std::memset(buffer->mAudioData, 0, byte_count);
    buffer->mAudioDataByteSize = byte_count;
    if (AudioQueueEnqueueBuffer(output.queue, buffer, 0U, nullptr) != noErr) {
      AudioQueueDispose(output.queue, true);
      output.queue = nullptr;
      output.buffers.fill(nullptr);
      return false;
    }
  }
  output.device_failed.store(false, std::memory_order_relaxed);
  if (AudioQueueStart(output.queue, nullptr) != noErr) {
    AudioQueueDispose(output.queue, true);
    output.queue = nullptr;
    output.buffers.fill(nullptr);
    return false;
  }
  return true;
}

std::uint32_t backend_queued_audio_frames(std::uint32_t channel) {
  return audio_output().engine.queued_frames(channel);
}

void backend_reset_audio_channel(std::uint32_t channel) {
  audio_output().engine.reset_channel(channel);
}

AudioTelemetry backend_audio_telemetry() {
  return audio_output().engine.telemetry();
}

std::uint64_t backend_audio_clock_frames() {
  return audio_output().engine.clock_frames();
}

} // namespace

bool submit_audio(const std::int16_t* interleaved_stereo,
                  std::uint32_t frame_count, std::uint32_t channel,
                  bool blocking, std::uint32_t sample_rate) {
  if (interleaved_stereo == nullptr || frame_count == 0U ||
      sample_rate != host_audio_sample_rate ||
      channel >= AudioEngine::channel_count)
    return false;
  auto& output = audio_output();
  set_audio_queue_callbacks(backend_queued_audio_frames,
                            backend_reset_audio_channel);
  set_audio_telemetry_callback(backend_audio_telemetry);
  set_audio_clock_frames_callback(backend_audio_clock_frames);
  if (!configure_audio_queue(output)) return false;
  const auto timeout = std::chrono::microseconds(
      audio_callback_timeout_microseconds(frame_count, sample_rate));
  return output.engine.submit(interleaved_stereo, frame_count, channel,
                              blocking, timeout) ==
         AudioEngine::SubmitResult::submitted;
}

void shutdown_audio() {
  auto& output = audio_output();
  std::lock_guard lock(output.configuration_mutex);
  output.engine.reset();
  if (output.queue == nullptr) return;
  AudioQueueStop(output.queue, true);
  AudioQueueDispose(output.queue, true);
  output.queue = nullptr;
  output.buffers.fill(nullptr);
  output.device_failed.store(false, std::memory_order_relaxed);
}

} // namespace refract::host
