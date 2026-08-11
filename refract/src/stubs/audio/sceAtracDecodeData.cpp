#include "atrac_state.hpp"

void sceAtracDecodeData(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static_cast<void>(implementation);
  const auto decoder = atrac_state::get(static_cast<int>(state.gpr[4]));
  if (decoder == nullptr) {
    state.gpr[2] = atrac_state::invalid_id;
    return;
  }
  std::int16_t* samples = nullptr;
  if (state.gpr[5] != 0U) {
    const auto maximum_bytes = static_cast<std::size_t>(4096U * 2U) *
                               sizeof(std::int16_t);
    samples = reinterpret_cast<std::int16_t*>(
        psprecomp::mapped_address(state, state.gpr[5], maximum_bytes));
    if (samples == nullptr) {
      state.gpr[2] = atrac_state::bad_data;
      return;
    }
  }
  std::uint32_t sample_count{};
  bool reached_end{};
  std::uint32_t result{};
  std::uint32_t remain{};
  {
    std::lock_guard lock(decoder->decoder_mutex);
    result = decoder->decode(samples, sample_count, reached_end);
    remain = decoder->remaining_frames();
    if (implementation.verbose && !decoder->audible_reported &&
        decoder->last_peak > 0.0001F) {
      decoder->audible_reported = true;
      std::fprintf(stderr,
                   "[psprism:atrac] audible id=%d frame=%llu samples=%u "
                   "peak=%.4f\n",
                   static_cast<int>(state.gpr[4]),
                   static_cast<unsigned long long>(decoder->decoded_frames),
                   sample_count, static_cast<double>(decoder->last_peak));
    }
  }
  atrac_state::write_u32(state, state.gpr[6], sample_count);
  atrac_state::write_u32(state, state.gpr[7], reached_end ? 1U : 0U);
  const auto remain_pointer =
      atrac_state::read_guest_u32(state, state.gpr[29] + 0x10U);
  atrac_state::write_u32(state, remain_pointer, remain);
  state.gpr[2] = result == atrac_state::all_data_decoded
                     ? atrac_state::all_data_decoded
                     : result;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
