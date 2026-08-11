void sceKernelPollMbx(Implementation& implementation,
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
  std::lock_guard lock(mailbox->mutex);
  if (mailbox->messages.empty()) {
    state.gpr[2] = mailbox_no_message;
    return;
  }
  *output = mailbox->messages.front().address;
  mailbox->messages.pop_front();
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
