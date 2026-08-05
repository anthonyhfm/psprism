void sceIoOpenAsync(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
      const auto* path = guest_string(state, state.gpr[4]);
      if (path == nullptr)
        return;
      std::uint64_t raw_disc_base = 0;
      const std::string_view psp_path(path);
      const auto lbn_marker = psp_path.find("/sce_lbn0x");
      const auto raw_disc =
          (psp_path.starts_with("disc0:") || psp_path.starts_with("umd0:")) &&
          lbn_marker != std::string_view::npos;
      if (raw_disc) {
        const auto begin = lbn_marker + std::string_view("/sce_lbn0x").size();
        const auto end = psp_path.find("_size0x", begin);
        if (end == std::string_view::npos) {
          state.gpr[2] = io_error;
          return;
        }
        const std::string lbn(psp_path.substr(begin, end - begin));
        char* parsed_end = nullptr;
        errno = 0;
        const auto lbn_value = std::strtoull(lbn.c_str(), &parsed_end, 16);
        if (errno != 0 || parsed_end == lbn.c_str() || *parsed_end != '\0') {
          state.gpr[2] = io_error;
          return;
        }
        raw_disc_base = lbn_value * 2048ULL;
      }
      const auto resolved = raw_disc ? implementation.disc_image
                                     : implementation.resolve_path(path);
      if ((state.gpr[5] & 0x0200U) != 0) {
        std::error_code ignored;
        std::filesystem::create_directories(resolved.parent_path(), ignored);
      }
      auto descriptor =
          ::open(resolved.c_str(), host_open_flags(state.gpr[5]),
                 static_cast<mode_t>(state.gpr[6]));
      if (descriptor >= 0 && raw_disc &&
          ::lseek(descriptor, static_cast<off_t>(raw_disc_base), SEEK_SET) < 0) {
        ::close(descriptor);
        descriptor = -1;
      }
      if (descriptor < 0) {
        state.gpr[2] = io_error;
      } else {
        const auto psp_descriptor = implementation.next_file++;
        implementation.files.emplace(psp_descriptor, descriptor);
        if (raw_disc)
          implementation.file_bases.emplace(psp_descriptor, raw_disc_base);
        implementation.async_results.emplace(psp_descriptor, psp_descriptor);
        state.gpr[2] = static_cast<std::uint32_t>(psp_descriptor);
      }
      return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
