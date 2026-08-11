#include "thread_status.hpp"

void sceKernelReceiveMbx(Implementation& implementation,
                         psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::Mailbox> mailbox;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found = implementation.mailboxes.find(
        static_cast<int>(state.gpr[4]));
    if (found != implementation.mailboxes.end())
      mailbox = found->second;
  }
  auto* output = guest_pointer<std::uint32_t>(state, state.gpr[5]);
  if (!mailbox) {
    state.gpr[2] = unknown_mailbox;
    return;
  }
  if (output == nullptr) {
    state.gpr[2] = unimplemented;
    return;
  }
  std::unique_lock lock(mailbox->mutex);
  const auto available = [&] {
    return !mailbox->messages.empty() || mailbox->deleted ||
           implementation.exit_requested;
  };
  std::uint32_t timeout_microseconds{};
  if (state.gpr[6] != 0U) {
    const auto* timeout = psprecomp::mapped_address(
        state, state.gpr[6], sizeof(timeout_microseconds));
    if (timeout == nullptr) {
      state.gpr[2] = unimplemented;
      return;
    }
    std::memcpy(&timeout_microseconds, timeout, sizeof(timeout_microseconds));
  }
  std::optional<thread_status::ScopedWait> wait_status;
  if (mailbox->messages.empty())
    wait_status.emplace(current_thread_id, 6, static_cast<int>(state.gpr[4]));
  std::uint32_t result{};
  std::uint32_t message{};
  {
    GuestExecutionPause pause(implementation);
    if (state.gpr[6] == 0U) {
      mailbox->changed.wait(lock, available);
    } else {
      if (!mailbox->changed.wait_for(
              lock, std::chrono::microseconds(timeout_microseconds),
              available)) {
        result = wait_timeout;
      }
    }
    if (result == 0U) {
      if (mailbox->deleted) {
        result = wait_deleted;
      } else if (!implementation.exit_requested) {
        message = mailbox->messages.front().address;
        mailbox->messages.pop_front();
      }
    }
    // A sender enters with the cooperative guest-execution lock held and then
    // takes the mailbox lock. Release the mailbox before GuestExecutionPause
    // reacquires the execution lock to preserve that ordering.
    lock.unlock();
  }
  if (result == 0U && !implementation.exit_requested)
    *output = message;
  state.gpr[2] = result;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
