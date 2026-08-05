void sceKernelDeleteThread(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::lock_guard lock(implementation.objects_mutex);
  const auto found =
      implementation.threads.find(static_cast<int>(state.gpr[4]));
  if (found == implementation.threads.end() ||
      found->second->host_thread.joinable())
    return;
  implementation.threads.erase(found);
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
