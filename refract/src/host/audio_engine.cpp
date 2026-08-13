#include "audio_engine.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace refract::host {

AudioEngine::SubmitResult AudioEngine::submit(
    const std::int16_t* interleaved_stereo, std::uint32_t frame_count,
    std::uint32_t channel, bool blocking, std::chrono::microseconds timeout) {
  if (interleaved_stereo == nullptr || frame_count == 0U ||
      channel >= channels_.size() ||
      frame_count > maximum_frames_per_channel)
    return SubmitResult::invalid;

  auto& target = channels_[channel];
  std::unique_lock lock(target.mutex);
  const auto can_submit = [&target, frame_count] {
    return target.queued_frames == 0U &&
           frame_count <= maximum_frames_per_channel;
  };
  if (!can_submit()) {
    if (!blocking) return SubmitResult::busy;
    if (!target.space_available.wait_for(lock, timeout, can_submit))
      return SubmitResult::timeout;
  }

  // Every PSP output call starts at an empty queue.  Keeping the write at
  // index zero makes submission a single bounded copy and leaves wraparound
  // exclusively on the consumer side.
  std::memcpy(target.samples.data(), interleaved_stereo,
              static_cast<std::size_t>(frame_count) * 2U *
                  sizeof(std::int16_t));
  target.read_frame = 0U;
  target.queued_frames = frame_count;
  return SubmitResult::submitted;
}

std::uint32_t AudioEngine::consume(std::int16_t* interleaved_stereo,
                                   std::uint32_t frame_count) noexcept {
  if (interleaved_stereo == nullptr || frame_count == 0U) return 0U;
  std::fill_n(interleaved_stereo, static_cast<std::size_t>(frame_count) * 2U,
              static_cast<std::int16_t>(0));

  std::uint32_t maximum_consumed = 0U;
  for (auto& source : channels_) {
    std::unique_lock lock(source.mutex, std::try_to_lock);
    if (!lock.owns_lock() || source.queued_frames == 0U) continue;

    const auto consumed = static_cast<std::uint32_t>(
        std::min<std::size_t>(frame_count, source.queued_frames));
    maximum_consumed = std::max(maximum_consumed, consumed);
    for (std::size_t frame = 0U; frame < consumed; ++frame) {
      const auto source_frame =
          (source.read_frame + frame) % maximum_frames_per_channel;
      for (std::size_t side = 0U; side < 2U; ++side) {
        const auto output_index = frame * 2U + side;
        const auto mixed =
            static_cast<std::int32_t>(interleaved_stereo[output_index]) +
            source.samples[source_frame * 2U + side];
        interleaved_stereo[output_index] = static_cast<std::int16_t>(
            std::clamp<std::int32_t>(mixed,
                                     std::numeric_limits<std::int16_t>::min(),
                                     std::numeric_limits<std::int16_t>::max()));
      }
    }
    source.read_frame =
        (source.read_frame + consumed) % maximum_frames_per_channel;
    source.queued_frames -= consumed;
    const bool drained = source.queued_frames == 0U;
    lock.unlock();
    if (drained) source.space_available.notify_all();
  }
  return maximum_consumed;
}

std::uint32_t AudioEngine::queued_frames(std::uint32_t channel) const {
  if (channel >= channels_.size()) return 0U;
  const auto& source = channels_[channel];
  std::lock_guard lock(source.mutex);
  return static_cast<std::uint32_t>(source.queued_frames);
}

void AudioEngine::reset_channel(std::uint32_t channel) {
  if (channel >= channels_.size()) return;
  auto& target = channels_[channel];
  {
    std::lock_guard lock(target.mutex);
    target.read_frame = 0U;
    target.queued_frames = 0U;
  }
  target.space_available.notify_all();
}

void AudioEngine::reset() {
  for (std::uint32_t channel = 0U; channel < channels_.size(); ++channel)
    reset_channel(channel);
}

} // namespace refract::host
