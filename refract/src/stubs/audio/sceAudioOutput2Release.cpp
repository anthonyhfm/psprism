void sceAudioOutput2Release(Implementation& implementation, psprecomp::State& state) {
  (void)implementation;
  state.gpr[2] = 0;
}
