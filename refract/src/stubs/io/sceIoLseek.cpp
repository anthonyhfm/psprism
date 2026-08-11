void sceIoLseek(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
      const auto psp_descriptor = static_cast<int>(state.gpr[4]);
      const auto descriptor = implementation.descriptor(psp_descriptor);
      if (descriptor < 0) {
        state.gpr[2] = io_error;
        state.gpr[3] = 0xffffffffU;
        return;
      }
      // Under the PSP EABI, the 64-bit offset is aligned to the even a2/a3
      // register pair.  The following argument is consequently in t0.
      const auto offset =
          io_state::signed_from_words(state.gpr[6], state.gpr[7]);
      const auto whence = state.gpr[8];
      const auto view = implementation.file_views.find(psp_descriptor);
      const auto sector_file =
          implementation.sector_files.contains(psp_descriptor);
      off_t result{-1};
      if (sector_file) {
        std::optional<std::uint64_t> origin;
        if (whence == SEEK_SET) {
          origin = 0U;
        } else if (whence == SEEK_CUR) {
          const auto current = ::lseek(descriptor, 0, SEEK_CUR);
          if (current >= 0)
            origin = io_state::complete_sector_count(
                static_cast<std::uint64_t>(current));
        } else if (whence == SEEK_END) {
          const auto end = ::lseek(descriptor, 0, SEEK_END);
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
                                std::numeric_limits<off_t>::max()) &&
            ::lseek(descriptor, static_cast<off_t>(*byte_offset), SEEK_SET) >=
                0)
          result = static_cast<off_t>(*logical);
      } else if (view == implementation.file_views.end()) {
        result = ::lseek(descriptor, offset, static_cast<int>(whence));
      } else {
        std::optional<std::uint64_t> origin;
        if (whence == SEEK_SET) {
          origin = 0U;
        } else if (whence == SEEK_END) {
          origin = view->second.size;
        } else if (whence == SEEK_CUR) {
          const auto current = ::lseek(descriptor, 0, SEEK_CUR);
          if (current >= 0 &&
              static_cast<std::uint64_t>(current) >= view->second.base)
            origin = static_cast<std::uint64_t>(current) - view->second.base;
        }
        const auto logical = origin ? io_state::add_signed(*origin, offset)
                                    : std::nullopt;
        if (logical &&
            *logical <= std::numeric_limits<std::uint64_t>::max() -
                            view->second.base &&
            view->second.base + *logical <=
                static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) &&
            ::lseek(descriptor,
                    static_cast<off_t>(view->second.base + *logical),
                    SEEK_SET) >= 0)
          result = static_cast<off_t>(*logical);
      }
      if (result < 0) {
        state.gpr[2] = io_error;
        state.gpr[3] = 0xffffffffU;
      } else {
        state.gpr[2] = static_cast<std::uint32_t>(result);
        state.gpr[3] =
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(result) >> 32U);
      }
      return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
