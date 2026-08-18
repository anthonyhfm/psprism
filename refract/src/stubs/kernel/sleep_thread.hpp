#pragma once

#include "thread_status.hpp"

inline void sleep_guest_thread(Implementation& implementation,
                               psprecomp::State& state) {
  std::shared_ptr<Implementation::GuestThread> thread;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found = implementation.threads.find(current_thread_id);
    if (found == implementation.threads.end()) {
      state.gpr[2] = 0x80020198U;
      return;
    }
    thread = found->second;
  }
  thread_status::ScopedWait sleep_status(current_thread_id, 1, 0);
  GuestExecutionPause pause(implementation);
  std::unique_lock lock(thread->sleep_mutex);
  if (thread->wakeup_count != 0U) {
    --thread->wakeup_count;
  } else {
    thread->sleep_changed.wait(lock, [&] {
      return thread->wakeup_count != 0U ||
             implementation.exit_requested.load(std::memory_order_relaxed);
    });
    if (thread->wakeup_count != 0U)
      --thread->wakeup_count;
  }
  state.gpr[2] = 0U;
}
