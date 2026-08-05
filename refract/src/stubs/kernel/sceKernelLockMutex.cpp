#include "thread_status.hpp"

void sceKernelLockMutex(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::Mutex> mutex;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found =
        implementation.mutexes.find(static_cast<int>(state.gpr[4]));
    if (found != implementation.mutexes.end())
      mutex = found->second;
  }
  if (!mutex) {
    state.gpr[2] = unimplemented;
    return;
  }
  std::unique_lock lock(mutex->mutex);
  const auto available = [&] {
    return mutex->lock_count == 0 || mutex->owner_thread_id == current_thread_id;
  };
  if (state.gpr[6] == 0) {
    mutex->changed.wait(lock, available);
  } else {
    std::uint32_t timeout_microseconds = 0;
    const auto* timeout = psprecomp::mapped_address(
        state, state.gpr[6], sizeof(timeout_microseconds));
    if (timeout == nullptr) {
      state.gpr[2] = unimplemented;
      return;
    }
    std::memcpy(&timeout_microseconds, timeout, sizeof(timeout_microseconds));
    if (!mutex->changed.wait_for(
            lock, std::chrono::microseconds(timeout_microseconds), available)) {
      state.gpr[2] = wait_timeout;
      return;
    }
  }
  mutex->owner_thread_id = current_thread_id;
  mutex->lock_count += static_cast<int>(state.gpr[5] ? state.gpr[5] : 1);
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
