void sceGeContinue(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  ge::DisplayList list;
  bool resume{};
  {
    std::lock_guard graphics_lock(implementation.graphics.mutex);
    const auto list_id = implementation.ge_scheduler.continue_lists();
    if (list_id >= 0) {
      list = *implementation.ge_scheduler.find(list_id);
      resume = true;
    }
  }
  if (resume)
    execute_ge_list(implementation, state, list.id, list.program_counter,
                    list.stall_address, list.callback_id);
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
