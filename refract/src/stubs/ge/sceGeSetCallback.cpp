void sceGeSetCallback(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto* data = psprecomp::mapped_address(state, state.gpr[4], 16U);
  if (data == nullptr) {
    state.gpr[2] = unimplemented;
    return;
  }
  std::uint32_t words[4]{};
  std::memcpy(words, data, sizeof(words));
  const auto uid = implementation.allocate_uid();
  {
    std::lock_guard lock(implementation.objects_mutex);
    implementation.ge_callbacks.emplace(
        uid, Implementation::GeCallback{words[0], words[1], words[2],
                                        words[3]});
  }
  if (implementation.verbose) {
    std::fprintf(stderr,
                 "[psprism:ge] callback uid=%d signal=%08x/%08x "
                 "finish=%08x/%08x\n",
                 uid, words[0], words[1], words[2], words[3]);
  }
  state.gpr[2] = static_cast<std::uint32_t>(uid);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
