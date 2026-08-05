#include "thread_status.hpp"

void sceKernelWaitSema(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::Semaphore> semaphore;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.semaphores.find(static_cast<int>(state.gpr[4]));
    if (found != implementation.semaphores.end())
      semaphore = found->second;
  }
  const auto requested = static_cast<int>(state.gpr[5]);
  if (!semaphore || requested <= 0 || requested > semaphore->maximum) {
    state.gpr[2] = unimplemented;
    return;
  }
  std::unique_lock lock(semaphore->mutex);
  std::optional<thread_status::ScopedWait> wait_status;
  if (semaphore->count < requested)
    wait_status.emplace(current_thread_id, 3, static_cast<int>(state.gpr[4]));
  const auto available = [&] { return semaphore->count >= requested; };
  if (state.gpr[6] == 0) {
    semaphore->changed.wait(lock, available);
  } else {
    std::uint32_t timeout_microseconds = 0;
    const auto* timeout = psprecomp::mapped_address(
        state, state.gpr[6], sizeof(timeout_microseconds));
    if (timeout == nullptr) {
      state.gpr[2] = unimplemented;
      return;
    }
    std::memcpy(&timeout_microseconds, timeout, sizeof(timeout_microseconds));
    if (!semaphore->changed.wait_for(
            lock, std::chrono::microseconds(timeout_microseconds), available)) {
      state.gpr[2] = wait_timeout;
      return;
    }
  }
  semaphore->count -= requested;
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
