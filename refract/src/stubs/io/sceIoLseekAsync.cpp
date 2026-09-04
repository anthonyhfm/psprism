void sceIoLseekAsync(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
      const auto psp_descriptor = static_cast<int>(state.gpr[4]);
      int descriptor = -1;
      bool has_view = false;
      io_state::FileView file_view{};
      bool sector_file = false;
      {
        std::lock_guard lock(implementation.io_mutex);
        if (psp_descriptor >= 0 && psp_descriptor <= 2) {
          descriptor = psp_descriptor;
        } else {
          const auto found = implementation.files.find(psp_descriptor);
          if (found != implementation.files.end())
            descriptor = found->second;
        }
        sector_file = implementation.sector_files.contains(psp_descriptor);
        const auto view = implementation.file_views.find(psp_descriptor);
        if (view != implementation.file_views.end()) {
          has_view = true;
          file_view = view->second;
        }
      }
      if (descriptor < 0) {
        state.gpr[2] = io_error;
        return;
      }
      // Under the PSP EABI, the 64-bit offset is aligned to the even a2/a3
      // register pair.  The following argument is consequently in t0.
      const auto offset =
          io_state::signed_from_words(state.gpr[6], state.gpr[7]);
      const auto whence = state.gpr[8];
      host_file::Offset result{-1};
      if (sector_file) {
        std::optional<std::uint64_t> origin;
        if (whence == SEEK_SET) {
          origin = 0U;
        } else if (whence == SEEK_CUR) {
          const auto current = host_file::seek(descriptor, 0, SEEK_CUR);
          if (current >= 0)
            origin = io_state::complete_sector_count(
                static_cast<std::uint64_t>(current));
        } else if (whence == SEEK_END) {
          const auto end = host_file::seek(descriptor, 0, SEEK_END);
          if (end >= 0)
            origin = io_state::complete_sector_count(
                static_cast<std::uint64_t>(end));
        }
        const auto logical = origin ? io_state::add_signed(*origin, offset)
                                    : std::nullopt;
        const auto byte_offset =
            logical ? io_state::sector_byte_offset(*logical) : std::nullopt;
        if (logical && byte_offset &&
            *byte_offset <= static_cast<std::uint64_t>(
                                std::numeric_limits<host_file::Offset>::max()) &&
            host_file::seek(descriptor,
                            static_cast<host_file::Offset>(*byte_offset),
                            SEEK_SET) >= 0)
          result = static_cast<host_file::Offset>(*logical);
      } else if (!has_view) {
        result = host_file::seek(descriptor, offset, static_cast<int>(whence));
      } else {
        std::optional<std::uint64_t> origin;
        if (whence == SEEK_SET) {
          origin = 0U;
        } else if (whence == SEEK_END) {
          origin = file_view.size;
        } else if (whence == SEEK_CUR) {
          const auto current = host_file::seek(descriptor, 0, SEEK_CUR);
          if (current >= 0 &&
              static_cast<std::uint64_t>(current) >= file_view.base)
            origin = static_cast<std::uint64_t>(current) - file_view.base;
        }
        const auto logical = origin ? io_state::add_signed(*origin, offset)
                                    : std::nullopt;
        if (logical &&
            *logical <= std::numeric_limits<std::uint64_t>::max() -
                            file_view.base &&
            file_view.base + *logical <=
                static_cast<std::uint64_t>(
                    std::numeric_limits<host_file::Offset>::max()) &&
            host_file::seek(descriptor,
                            static_cast<host_file::Offset>(file_view.base +
                                                           *logical),
                            SEEK_SET) >= 0)
          result = static_cast<host_file::Offset>(*logical);
      }
      {
        std::lock_guard lock(implementation.io_mutex);
        implementation.async_results[psp_descriptor] =
            result < 0 ? static_cast<std::int64_t>(static_cast<std::int32_t>(io_error))
                       : result;
      }
      state.gpr[2] = 0U;
      state.gpr[3] = 0U;
      return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
