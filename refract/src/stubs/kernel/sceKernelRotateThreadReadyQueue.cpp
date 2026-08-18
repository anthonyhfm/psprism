void sceKernelRotateThreadReadyQueue(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  {
    GuestExecutionPause pause(implementation);
    std::this_thread::yield();
  }
  state.gpr[2] = 0;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
