#include "audio_engine.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace refract::host {

AudioEngine::AudioEngine()
    : channels_(std::make_unique<std::array<Channel, channel_count>>()),
      mix_samples_(std::make_unique<std::array<
                       std::int32_t, maximum_frames_per_channel * 2U>>()) {}

AudioEngine::SubmitResult AudioEngine::submit(
    const std::int16_t* interleaved_stereo, std::uint32_t frame_count,
    std::uint32_t channel, bool blocking, std::chrono::microseconds timeout) {
  if (interleaved_stereo == nullptr || frame_count == 0U ||
      channel >= channels_->size() ||
      frame_count > maximum_frames_per_channel) {
    rejected_submissions_.fetch_add(1U, std::memory_order_relaxed);
    return SubmitResult::invalid;
  }

  auto& target = (*channels_)[channel];
  std::unique_lock lock(target.submit_mutex);
  const auto wait_until = [&target, &lock, timeout](const auto& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) return false;
      // The audio callback deliberately never takes submit_mutex. Polling at
      // PSP audio-tick granularity closes the condition-variable lost-wakeup
      // window without introducing a real-time priority inversion.
      const auto remaining = deadline - now;
      const auto poll_interval =
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::milliseconds(1));
      target.space_available.wait_for(
          lock, std::min(remaining, poll_interval));
    }
    return true;
  };
  const auto queued = [&target] {
    const auto w = target.write_index_.load(std::memory_order_relaxed);
    const auto r = target.read_index_.load(std::memory_order_acquire);
    return w - r;
  };
  const auto queued_before = queued();
  if (!blocking && queued_before != 0U) {
    overrun_submissions_.fetch_add(1U, std::memory_order_relaxed);
    dropped_frames_.fetch_add(frame_count, std::memory_order_relaxed);
    return SubmitResult::busy;
  }

  constexpr std::size_t cap = maximum_frames_per_channel;
  const auto has_capacity = [&queued, frame_count] {
    return queued() <= cap - frame_count;
  };
  if (!has_capacity() && (!blocking || !wait_until(has_capacity))) {
    overrun_submissions_.fetch_add(1U, std::memory_order_relaxed);
    dropped_frames_.fetch_add(frame_count, std::memory_order_relaxed);
    return blocking ? SubmitResult::timeout : SubmitResult::busy;
  }

  const auto w = target.write_index_.load(std::memory_order_relaxed);
  constexpr std::size_t mask = cap - 1U;
  const auto start_frame = w & mask;
  const auto frames_to_end = cap - start_frame;
  const auto first_frames = std::min<std::size_t>(frame_count, frames_to_end);

  std::memcpy(&target.samples[start_frame * 2U], interleaved_stereo,
              first_frames * 2U * sizeof(std::int16_t));
  if (first_frames < frame_count) {
    const auto second_frames =
        static_cast<std::size_t>(frame_count) - first_frames;
    std::memcpy(&target.samples[0], interleaved_stereo + first_frames * 2U,
                second_frames * 2U * sizeof(std::int16_t));
  }
  target.write_index_.store(w + frame_count, std::memory_order_release);

  submitted_frames_.fetch_add(frame_count, std::memory_order_relaxed);
  const auto depth =
      queued_frames_.fetch_add(frame_count, std::memory_order_relaxed) +
      frame_count;
  auto peak = peak_queued_frames_.load(std::memory_order_relaxed);
  while (peak < depth &&
         !peak_queued_frames_.compare_exchange_weak(
             peak, depth, std::memory_order_relaxed,
             std::memory_order_relaxed)) {
  }

  // The PSP queues a blocking write immediately, then keeps the caller asleep
  // until the audio that was already queued has drained. Waiting before the
  // copy leaves a host-callback-sized hole between every pair of guest blocks.
  if (blocking && queued_before != 0U) {
    const auto started_playing = [&target, w] {
      return target.read_index_.load(std::memory_order_acquire) >= w;
    };
    // Once accepted, a PSP blocking write remains asleep until the samples
    // that preceded it have drained. A timeout based only on the newly
    // submitted buffer lets a queued stream run ahead and eventually loses
    // its pacing. Polling also closes the callback's lost-notification window.
    while (!started_playing())
      target.space_available.wait_for(lock, std::chrono::milliseconds(1));
  }
  return SubmitResult::submitted;
}

std::uint32_t AudioEngine::consume(std::int16_t* interleaved_stereo,
                                   std::uint32_t frame_count) noexcept {
  if (interleaved_stereo == nullptr || frame_count == 0U) return 0U;
  if (frame_count > maximum_frames_per_channel) return 0U;
  const auto sample_count = static_cast<std::size_t>(frame_count) * 2U;
  std::fill_n(mix_samples_->data(), sample_count, 0);
  callback_count_.fetch_add(1U, std::memory_order_relaxed);
  rendered_frames_.fetch_add(frame_count, std::memory_order_relaxed);

  std::uint32_t maximum_consumed = 0U;
  constexpr std::size_t cap = maximum_frames_per_channel;
  constexpr std::size_t mask = cap - 1U;

  for (auto& source : *channels_) {
    const auto w = source.write_index_.load(std::memory_order_acquire);
    const auto r = source.read_index_.load(std::memory_order_relaxed);
    if (w == r) continue;

    const auto available = static_cast<std::uint32_t>(w - r);
    const auto consumed = std::min(frame_count, available);
    maximum_consumed = std::max(maximum_consumed, consumed);

    for (std::size_t frame = 0U; frame < consumed; ++frame) {
      const auto source_frame = (r + frame) & mask;
      for (std::size_t side = 0U; side < 2U; ++side) {
        const auto output_index = frame * 2U + side;
        (*mix_samples_)[output_index] +=
            source.samples[source_frame * 2U + side];
      }
    }

    source.read_index_.store(r + consumed, std::memory_order_release);
    queued_frames_.fetch_sub(consumed, std::memory_order_relaxed);

    source.space_available.notify_all();
  }

  for (std::size_t sample = 0U; sample < sample_count; ++sample) {
    interleaved_stereo[sample] = static_cast<std::int16_t>(
        std::clamp<std::int32_t>((*mix_samples_)[sample],
                                 std::numeric_limits<std::int16_t>::min(),
                                 std::numeric_limits<std::int16_t>::max()));
  }

  consumed_frames_.fetch_add(maximum_consumed, std::memory_order_relaxed);
  if (maximum_consumed < frame_count) {
    underrun_callbacks_.fetch_add(1U, std::memory_order_relaxed);
    underrun_frames_.fetch_add(frame_count - maximum_consumed,
                               std::memory_order_relaxed);
  }
  return maximum_consumed;
}

std::uint32_t AudioEngine::queued_frames(std::uint32_t channel) const noexcept {
  if (channel >= channels_->size()) return 0U;
  const auto& source = (*channels_)[channel];
  const auto w = source.write_index_.load(std::memory_order_acquire);
  const auto r = source.read_index_.load(std::memory_order_acquire);
  return static_cast<std::uint32_t>(w >= r ? w - r : 0U);
}

AudioTelemetry AudioEngine::telemetry() const noexcept {
  return AudioTelemetry{
      submitted_frames_.load(std::memory_order_relaxed),
      consumed_frames_.load(std::memory_order_relaxed),
      queued_frames_.load(std::memory_order_relaxed),
      peak_queued_frames_.load(std::memory_order_relaxed),
      callback_count_.load(std::memory_order_relaxed),
      underrun_callbacks_.load(std::memory_order_relaxed),
      underrun_frames_.load(std::memory_order_relaxed),
      overrun_submissions_.load(std::memory_order_relaxed),
      dropped_frames_.load(std::memory_order_relaxed),
      rejected_submissions_.load(std::memory_order_relaxed),
      callback_lock_contentions_.load(std::memory_order_relaxed),
      device_resets_.load(std::memory_order_relaxed),
  };
}

void AudioEngine::reset_channel(std::uint32_t channel) {
  if (channel >= channels_->size()) return;
  auto& target = (*channels_)[channel];
  std::lock_guard lock(target.submit_mutex);
  const auto cur_w = target.write_index_.load(std::memory_order_relaxed);
  const auto cur_r = target.read_index_.load(std::memory_order_relaxed);
  const auto discarded = cur_w - cur_r;
  if (discarded != 0U) {
    target.read_index_.store(cur_w, std::memory_order_release);
    queued_frames_.fetch_sub(discarded, std::memory_order_relaxed);
    dropped_frames_.fetch_add(discarded, std::memory_order_relaxed);
  }
  target.space_available.notify_all();
}

void AudioEngine::device_reset() {
  reset();
  device_resets_.fetch_add(1U, std::memory_order_relaxed);
}

void AudioEngine::reset() {
  for (std::uint32_t channel = 0U; channel < channels_->size(); ++channel)
    reset_channel(channel);
}

} // namespace refract::host
