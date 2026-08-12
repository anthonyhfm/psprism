void sceMpegRegistStream(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  (void)implementation;
  static std::atomic<std::uint32_t> next_stream{1U};
  state.gpr[2] = next_stream.fetch_add(1U);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
