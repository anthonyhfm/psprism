void sceMp3ReserveMp3Handle(Implementation& implementation, psprecomp::State& state) {
  (void)implementation;
  state.gpr[2] = 1; // return handle ID > 0
}
