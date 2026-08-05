void sceNetInetInit(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)

#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
