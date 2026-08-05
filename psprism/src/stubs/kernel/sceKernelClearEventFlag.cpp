void sceKernelClearEventFlag(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
      std::shared_ptr<Implementation::EventFlag> event;
      {
        std::lock_guard lock(implementation.objects_mutex);
        const auto found =
            implementation.event_flags.find(static_cast<int>(state.gpr[4]));
        if (found == implementation.event_flags.end())
          return;
        event = found->second;
      }
      {
        std::lock_guard lock(event->mutex);
        event->bits &= state.gpr[5];
      }
      event->changed.notify_all();
      state.gpr[2] = 0;
      return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
