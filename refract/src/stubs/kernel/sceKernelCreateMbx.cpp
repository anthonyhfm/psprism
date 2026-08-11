void sceKernelCreateMbx(Implementation& implementation,
                        psprecomp::State& state) {
#if !defined(__PSP__)
  const auto* name = guest_string(state, state.gpr[4]);
  if (name == nullptr) {
    state.gpr[2] = unimplemented;
    return;
  }
  auto mailbox = std::make_shared<Implementation::Mailbox>();
  mailbox->name = name;
  mailbox->attributes = state.gpr[5];
  std::lock_guard lock(implementation.objects_mutex);
  const auto uid = implementation.allocate_uid();
  implementation.mailboxes.emplace(uid, mailbox);
  if (implementation.verbose)
    std::fprintf(stderr, "[psprism:mbx] create uid=%d name=%s attr=%08x\n",
                 uid, mailbox->name.c_str(), mailbox->attributes);
  state.gpr[2] = static_cast<std::uint32_t>(uid);
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
