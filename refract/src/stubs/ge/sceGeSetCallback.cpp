void sceGeSetCallback(Implementation& implementation, psprecomp::State& state) {
  (void)implementation;
  state.gpr[2] = 1; // return valid callback ID (>0)
}
