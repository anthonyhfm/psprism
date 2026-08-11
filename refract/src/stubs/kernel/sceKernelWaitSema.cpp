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
  static thread_local std::uint32_t logged_waits{};
  static thread_local std::uint32_t last_wait_uid{};
  const auto log_wait = implementation.verbose &&
                        (logged_waits < 16U ||
                         (logged_waits & 0xffffU) == 0U ||
                         last_wait_uid != state.gpr[4]);
  ++logged_waits;
  last_wait_uid = state.gpr[4];
  if (log_wait) {
    std::fprintf(stderr,
                 "[psprism:sema] wait thread=%d uid=%u name=%s request=%d "
                 "count=%d timeout=%08x\n",
                 current_thread_id, state.gpr[4], semaphore->name.c_str(),
                 requested, semaphore->count, state.gpr[6]);
  }
  std::optional<thread_status::ScopedWait> wait_status;
  if (semaphore->count < requested) {
    wait_status.emplace(current_thread_id, 3, static_cast<int>(state.gpr[4]));
  }
  const auto available = [&] { return semaphore->count >= requested; };
  {
    GuestExecutionPause pause(implementation);
    if (state.gpr[6] == 0) {
      semaphore->changed.wait(lock, available);
    } else {
      std::uint32_t timeout_microseconds = 0;
      const auto* timeout = psprecomp::mapped_address(
          state, state.gpr[6], sizeof(timeout_microseconds));
      if (timeout == nullptr) {
        state.gpr[2] = unimplemented;
        lock.unlock();
        return;
      }
      std::memcpy(&timeout_microseconds, timeout, sizeof(timeout_microseconds));
      if (!semaphore->changed.wait_for(
              lock, std::chrono::microseconds(timeout_microseconds), available)) {
        state.gpr[2] = wait_timeout;
        lock.unlock();
        return;
      }
    }
    semaphore->count -= requested;
    lock.unlock();
  }
  if (log_wait) {
    std::fprintf(stderr,
                 "[psprism:sema] acquired thread=%d uid=%u count=%d\n",
                 current_thread_id, state.gpr[4], semaphore->count);
  }
  state.gpr[2] = 0;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
