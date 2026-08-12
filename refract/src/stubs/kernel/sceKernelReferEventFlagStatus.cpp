void sceKernelReferEventFlagStatus(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  struct EventFlagStatus {
    std::uint32_t size{};
    std::array<char, 32> name{};
    std::uint32_t attributes{};
    std::uint32_t initial_bits{};
    std::uint32_t current_bits{};
    std::int32_t waiting_threads{};
  };
  static_assert(sizeof(EventFlagStatus) == 52U);

  std::shared_ptr<Implementation::EventFlag> event;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.event_flags.find(static_cast<int>(state.gpr[4]));
    if (found == implementation.event_flags.end()) {
      state.gpr[2] = unimplemented;
      return;
    }
    event = found->second;
  }
  auto* status = guest_pointer<EventFlagStatus>(state, state.gpr[5]);
  if (status == nullptr) {
    state.gpr[2] = io_error;
    return;
  }
  if (status->size == 0U) {
    state.gpr[2] = 0U;
    return;
  }

  EventFlagStatus snapshot;
  snapshot.size = sizeof(snapshot);
  {
    std::lock_guard lock(event->mutex);
    std::memcpy(snapshot.name.data(), event->name.data(),
                std::min(event->name.size(), snapshot.name.size() - 1U));
    snapshot.attributes = event->attributes;
    snapshot.initial_bits = event->initial_bits;
    snapshot.current_bits = event->bits;
    snapshot.waiting_threads =
        static_cast<std::int32_t>(event->waiting_threads);
  }
  *status = snapshot;
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
