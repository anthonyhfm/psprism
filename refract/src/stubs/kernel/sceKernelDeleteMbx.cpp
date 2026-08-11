void sceKernelDeleteMbx(Implementation& implementation,
                        psprecomp::State& state) {
#if !defined(__PSP__)
  std::shared_ptr<Implementation::Mailbox> mailbox;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found = implementation.mailboxes.find(
        static_cast<int>(state.gpr[4]));
    if (found == implementation.mailboxes.end()) {
      state.gpr[2] = unknown_mailbox;
      return;
    }
    mailbox = found->second;
    implementation.mailboxes.erase(found);
  }
  {
    std::lock_guard lock(mailbox->mutex);
    mailbox->deleted = true;
  }
  mailbox->changed.notify_all();
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
