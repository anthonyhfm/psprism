void sceKernelSendMbx(Implementation& implementation,
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
  const auto message_address = state.gpr[5];
  const auto* message = psprecomp::mapped_address(state, message_address, 8U);
  if (!mailbox) {
    state.gpr[2] = unknown_mailbox;
    return;
  }
  if (message == nullptr) {
    state.gpr[2] = unimplemented;
    return;
  }
  const auto priority = message[4];
  {
    std::lock_guard lock(mailbox->mutex);
    if (!mailbox_state::enqueue(mailbox->messages, mailbox->attributes,
                                message_address, priority)) {
      state.gpr[2] = 0x800201c9U;
      return;
    }
  }
  mailbox->changed.notify_one();
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
