void sceKernelCreateThread(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  const auto* thread_name = guest_string(state, state.gpr[4]);
  const auto requested_stack = std::max<std::uint32_t>(state.gpr[7], 0x4000U);
  std::lock_guard lock(implementation.objects_mutex);
  const auto stack = implementation.allocate_stack(requested_stack);
  const auto tls = implementation.allocate_heap(0x100U, 64U);
  if (stack == 0 || tls == 0 ||
      !implementation.configuration.guest_executor) {
    state.gpr[2] = out_of_memory;
    return;
  }
  auto thread = std::make_shared<Implementation::GuestThread>();
  thread->uid = implementation.allocate_uid();
  thread->name = thread_name != nullptr ? thread_name : "guest-thread";
  thread->entry = state.gpr[5];
  thread->priority = state.gpr[6];
  thread->stack_address = stack;
  thread->stack_size = requested_stack;
  thread->tls_address = tls;
  implementation.threads.emplace(thread->uid, thread);
  state.gpr[2] = static_cast<std::uint32_t>(thread->uid);
  if (implementation.verbose)
    std::fprintf(stderr,
                 "[psprism:thread] create uid=%d name=%s entry=%08x\n",
                 thread->uid, thread->name.c_str(), thread->entry);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
