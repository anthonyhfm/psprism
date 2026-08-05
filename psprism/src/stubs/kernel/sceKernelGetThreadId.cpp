void sceKernelGetThreadId(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  state.gpr[2] = static_cast<std::uint32_t>(current_thread_id);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
