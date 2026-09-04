#include "host.hpp"
#include "audio_engine.hpp"

#if !defined(_WIN32)
#error "This psprism audio backend requires Windows"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xaudio2.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace refract::host {
namespace {

constexpr std::uint32_t host_audio_sample_rate = 44100U;
constexpr std::uint32_t host_audio_buffer_frames = 512U;
constexpr std::size_t host_audio_buffer_count = 4U;

struct AudioOutput;

class VoiceCallback final : public IXAudio2VoiceCallback {
public:
  explicit VoiceCallback(AudioOutput& owner) : owner_(owner) {}

  void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
  void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
  void STDMETHODCALLTYPE OnStreamEnd() override {}
  void STDMETHODCALLTYPE OnBufferStart(void*) override {}
  void STDMETHODCALLTYPE OnBufferEnd(void*) override;
  void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
  void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override;

private:
  AudioOutput& owner_;
};

struct AudioOutput {
  AudioOutput() : callback(*this) {}
  ~AudioOutput() { stop(); }

  bool start() {
    std::lock_guard lock(configuration_mutex);
    if (source != nullptr && !failed.load(std::memory_order_relaxed))
      return true;
    stop_locked();

    if (FAILED(XAudio2Create(&xaudio, 0U, XAUDIO2_DEFAULT_PROCESSOR)))
      return false;
    if (FAILED(xaudio->CreateMasteringVoice(&mastering))) {
      stop_locked();
      return false;
    }
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2U;
    format.nSamplesPerSec = host_audio_sample_rate;
    format.wBitsPerSample = 16U;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8U;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    if (FAILED(xaudio->CreateSourceVoice(&source, &format, 0U,
                                         XAUDIO2_DEFAULT_FREQ_RATIO,
                                         &callback))) {
      stop_locked();
      return false;
    }
    stopping = false;
    failed.store(false, std::memory_order_relaxed);
    worker = std::thread([this] { feed(); });
    return true;
  }

  void stop() {
    std::lock_guard lock(configuration_mutex);
    stop_locked();
  }

  void buffer_finished() {
    std::lock_guard lock(queue_mutex);
    if (queued_buffers != 0U) --queued_buffers;
    queue_changed.notify_all();
  }

  void voice_failed() {
    failed.store(true, std::memory_order_relaxed);
    buffer_finished();
  }

  std::mutex configuration_mutex;
  std::mutex queue_mutex;
  std::condition_variable queue_changed;
  IXAudio2* xaudio{};
  IXAudio2MasteringVoice* mastering{};
  IXAudio2SourceVoice* source{};
  VoiceCallback callback;
  AudioEngine engine;
  std::array<std::array<std::int16_t, host_audio_buffer_frames * 2U>,
             host_audio_buffer_count>
      buffers{};
  std::thread worker;
  std::size_t next_buffer{};
  std::size_t queued_buffers{};
  bool stopping{};
  std::atomic<bool> failed{};

private:
  void feed() {
    if (FAILED(source->Start())) {
      failed.store(true, std::memory_order_relaxed);
      return;
    }
    for (;;) {
      std::unique_lock lock(queue_mutex);
      queue_changed.wait(lock, [this] {
        return stopping || failed.load(std::memory_order_relaxed) ||
               queued_buffers < buffers.size();
      });
      if (stopping || failed.load(std::memory_order_relaxed)) return;
      auto& samples = buffers[next_buffer];
      next_buffer = (next_buffer + 1U) % buffers.size();
      ++queued_buffers;
      lock.unlock();

      engine.consume(samples.data(), host_audio_buffer_frames);
      XAUDIO2_BUFFER buffer{};
      buffer.AudioBytes = static_cast<UINT32>(samples.size() *
                                               sizeof(samples.front()));
      buffer.pAudioData = reinterpret_cast<const BYTE*>(samples.data());
      if (FAILED(source->SubmitSourceBuffer(&buffer))) {
        failed.store(true, std::memory_order_relaxed);
        buffer_finished();
        return;
      }
    }
  }

  void stop_locked() {
    {
      std::lock_guard queue_lock(queue_mutex);
      stopping = true;
    }
    queue_changed.notify_all();
    if (worker.joinable()) worker.join();
    if (source != nullptr) {
      source->Stop();
      source->DestroyVoice();
      source = nullptr;
    }
    if (mastering != nullptr) {
      mastering->DestroyVoice();
      mastering = nullptr;
    }
    if (xaudio != nullptr) {
      xaudio->Release();
      xaudio = nullptr;
    }
    {
      std::lock_guard queue_lock(queue_mutex);
      queued_buffers = 0U;
      next_buffer = 0U;
    }
    engine.device_reset();
  }
};

void VoiceCallback::OnBufferEnd(void*) { owner_.buffer_finished(); }

void VoiceCallback::OnVoiceError(void*, HRESULT) { owner_.voice_failed(); }

AudioOutput& audio_output() {
  static AudioOutput value;
  return value;
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
  if (!output.start()) return false;
  const auto timeout = std::chrono::microseconds(
      audio_callback_timeout_microseconds(frame_count, sample_rate));
  return output.engine.submit(interleaved_stereo, frame_count, channel,
                              blocking, timeout) ==
         AudioEngine::SubmitResult::submitted;
}

void shutdown_audio() { audio_output().stop(); }

} // namespace refract::host
