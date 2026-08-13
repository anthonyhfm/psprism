void sceGeListEnQueueHead(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  int list_id{};
  {
    std::lock_guard graphics_lock(implementation.graphics.mutex);
    list_id = implementation.ge_scheduler.enqueue(
        state.gpr[4], state.gpr[5], state.gpr[6], true);
  }
  execute_ge_list(implementation, state, list_id, state.gpr[4], state.gpr[5],
                  state.gpr[6]);
  state.gpr[2] = static_cast<std::uint32_t>(list_id);
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
