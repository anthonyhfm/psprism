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
  std::optional<std::uint32_t> timeout_microseconds;
  if (state.gpr[6] != 0U) {
    const auto* timeout = psprecomp::mapped_address(
        state, state.gpr[6], sizeof(*timeout_microseconds));
    if (timeout == nullptr) {
      state.gpr[2] = unimplemented;
      return;
    }
    timeout_microseconds.emplace();
    std::memcpy(&*timeout_microseconds, timeout,
                sizeof(*timeout_microseconds));
  }
  std::unique_lock lock(semaphore->mutex);
  static thread_local std::uint32_t logged_waits{};
  const auto log_wait = implementation.verbose &&
                        (logged_waits < 16U ||
                         (logged_waits & 0xffffU) == 0U);
  ++logged_waits;
  if (log_wait) {
    std::fprintf(stderr,
                 "[psprism:sema] wait thread=%d uid=%u name=%s request=%d "
                 "count=%d timeout=%08x\n",
                 current_thread_id, state.gpr[4], semaphore->name.c_str(),
                 requested, semaphore->count, state.gpr[6]);
  }
  const auto queued = semaphore->count < requested ||
                      !semaphore->waiting_tickets.empty();
  const auto wait_ticket = semaphore->next_wait_ticket++;
  if (queued) semaphore->waiting_tickets.push_back(wait_ticket);
  std::optional<thread_status::ScopedWait> wait_status;
  if (queued) {
    wait_status.emplace(current_thread_id, 3, static_cast<int>(state.gpr[4]));
  }
  const auto available = [&] {
    return implementation.exit_requested ||
           (!queued || (!semaphore->waiting_tickets.empty() &&
                        semaphore->waiting_tickets.front() == wait_ticket &&
                        semaphore->count >= requested));
  };
  const auto remove_waiter = [&] {
    if (!queued) return;
    const auto found = std::find(semaphore->waiting_tickets.begin(),
                                 semaphore->waiting_tickets.end(),
                                 wait_ticket);
    if (found != semaphore->waiting_tickets.end())
      semaphore->waiting_tickets.erase(found);
  };
  {
    GuestExecutionPause pause(implementation);
    if (!timeout_microseconds.has_value()) {
      semaphore->changed.wait(lock, available);
    } else {
      if (!semaphore->changed.wait_for(
              lock, std::chrono::microseconds(*timeout_microseconds),
              available)) {
        remove_waiter();
        state.gpr[2] = wait_timeout;
        lock.unlock();
        semaphore->changed.notify_all();
        return;
      }
    }
    if (!implementation.exit_requested) {
      remove_waiter();
      semaphore->count -= requested;
    } else {
      remove_waiter();
    }
    lock.unlock();
    if (queued) semaphore->changed.notify_all();
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
