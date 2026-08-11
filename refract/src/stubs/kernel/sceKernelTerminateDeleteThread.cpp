void sceKernelTerminateDeleteThread(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  if (implementation.verbose) {
    std::fprintf(stderr,
                 "[psprism:thread] terminate-delete caller=%d uid=%u\n",
                 current_thread_id, state.gpr[4]);
  }
  state.gpr[2] = 0U;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
