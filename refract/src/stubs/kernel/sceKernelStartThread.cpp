void sceKernelStartThread(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::GuestThread> thread;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.threads.find(static_cast<int>(state.gpr[4]));
    if (found == implementation.threads.end() ||
        found->second->host_thread.joinable())
      return;
    thread = found->second;
  }
  const auto argument_size = state.gpr[5];
  const auto argument_pointer = state.gpr[6];
  if (implementation.verbose)
    std::fprintf(stderr,
                 "[psprism:thread] launch uid=%d args=%u argp=%08x\n",
                 thread->uid, argument_size, argument_pointer);
  thread->state = std::make_shared<psprecomp::State>();
  thread->state->memory = implementation.memory;
  thread->state->memory_size = implementation.memory_size;
  thread->state->memory_base = implementation.memory_base;
  thread->state->direct_memory_access = false;
  prepare_state_for_thread(implementation, *thread->state);
  thread->state->pc = thread->entry;
  thread->state->gpr[4] = argument_size;
  thread->state->gpr[5] = argument_pointer;
  thread->state->gpr[26] = thread->tls_address;
  thread->state->gpr[27] = thread->tls_address + 0x80U;
  thread->state->gpr[29] = thread->stack_address + thread->stack_size - 64U;
  if (argument_size != 0) {
    const auto copied_size = (argument_size + 15U) & ~15U;
    if (copied_size > thread->stack_size - 64U) {
      state.gpr[2] = unimplemented;
      return;
    }
    const auto copied_argument_pointer =
        thread->stack_address + thread->stack_size - copied_size;
    const auto* source =
        psprecomp::mapped_address(state, argument_pointer, argument_size);
    auto* destination = psprecomp::mapped_address(
        *thread->state, copied_argument_pointer, argument_size);
    if (source == nullptr || destination == nullptr) {
      state.gpr[2] = unimplemented;
      return;
    }
    std::memmove(destination, source, argument_size);
    thread->state->gpr[5] = copied_argument_pointer;
    thread->state->gpr[29] = copied_argument_pointer - 64U;
  }
  thread->state->gpr[31] = return_address;
  const auto verbose = implementation.verbose;
  thread->host_thread = std::thread([thread, verbose, &implementation] {
    current_thread_id = thread->uid;
    if (verbose)
      std::fprintf(stderr, "[psprism:thread] start uid=%d name=%s\n",
                   thread->uid, thread->name.c_str());
    execute_guest(implementation, *thread->state);
    thread->result = static_cast<std::int32_t>(thread->state->gpr[2]);
    thread->finished = true;
    if (verbose) {
      std::uint32_t saved_ra = 0;
      if (const auto* saved_ra_address = psprecomp::mapped_address(
              *thread->state, thread->state->gpr[29], sizeof(saved_ra)))
        std::memcpy(&saved_ra, saved_ra_address, sizeof(saved_ra));
      std::fprintf(
          stderr,
          "[psprism:thread] stop uid=%d reason=%u pc=%08x result=%d "
          "fault=%08x fault_pc=%08x insn=%08x sp=%08x ra=%08x "
          "a0=%08x a1=%08x a2=%08x a3=%08x saved_ra=%08x\n",
          thread->uid, static_cast<unsigned>(thread->state->stop_reason),
          thread->state->pc, thread->result, thread->state->fault_address,
          thread->state->fault_pc, thread->state->fault_instruction,
          thread->state->gpr[29], thread->state->gpr[31],
          thread->state->gpr[4], thread->state->gpr[5],
          thread->state->gpr[6], thread->state->gpr[7], saved_ra);
    }
  });
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
