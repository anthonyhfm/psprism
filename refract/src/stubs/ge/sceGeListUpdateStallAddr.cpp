void sceGeListUpdateStallAddr(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto list_id = static_cast<int>(state.gpr[4]);
  const auto stall_address = state.gpr[5];
  Implementation::GeList list;
  {
    std::lock_guard graphics_lock(implementation.graphics.mutex);
    const auto found = implementation.ge_lists.find(list_id);
    if (found == implementation.ge_lists.end() || found->second.ended) {
      state.gpr[2] = 0x80000100U;
      return;
    }
    list = found->second;
  }
  execute_ge_list(implementation, state, list_id, list.program_counter,
                  stall_address, list.callback_id);
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
