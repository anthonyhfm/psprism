void sceIoOpen(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
      const auto* path = guest_string(state, state.gpr[4]);
      if (path == nullptr)
        return;
      const std::string_view psp_path(path);
      if (psp_path.empty()) {
        state.gpr[2] = io_error;
        return;
      }
      const auto raw_disc = io_state::is_raw_disc_path(psp_path);
      const auto whole_disc = io_state::is_whole_disc_path(psp_path);
      auto raw_disc_view = io_state::parse_raw_disc_view(psp_path);
      if (raw_disc && !raw_disc_view) {
        state.gpr[2] = io_error;
        return;
      }
      const auto resolved = raw_disc || whole_disc
                                ? implementation.disc_image
                                : implementation.resolve_path(path);
      if ((state.gpr[5] & 0x0200U) != 0) {
        std::error_code ignored;
        std::filesystem::create_directories(resolved.parent_path(), ignored);
      }
      auto descriptor =
          ::open(resolved.c_str(), host_open_flags(state.gpr[5]),
                 static_cast<mode_t>(state.gpr[6]));
      if (descriptor >= 0 && raw_disc) {
        const auto end = ::lseek(descriptor, 0, SEEK_END);
        raw_disc_view = end < 0
                            ? std::nullopt
                            : io_state::complete_file_view(
                                  *raw_disc_view,
                                  static_cast<std::uint64_t>(end));
        if (!raw_disc_view ||
            ::lseek(descriptor, static_cast<off_t>(raw_disc_view->base),
                    SEEK_SET) < 0) {
          ::close(descriptor);
          descriptor = -1;
        }
      }
      if (descriptor < 0) {
        state.gpr[2] = io_state::error_from_errno(errno);
      } else {
        const auto psp_descriptor = implementation.next_file++;
        implementation.files.emplace(psp_descriptor, descriptor);
        if (raw_disc)
          implementation.file_views.emplace(psp_descriptor, *raw_disc_view);
        if (whole_disc)
          implementation.sector_files.emplace(psp_descriptor);
        state.gpr[2] = static_cast<std::uint32_t>(psp_descriptor);
      }
      return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
