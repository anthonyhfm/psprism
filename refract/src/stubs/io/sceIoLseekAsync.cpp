void sceIoLseekAsync(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
      const auto descriptor =
          implementation.descriptor(static_cast<int>(state.gpr[4]));
      if (descriptor < 0) {
        state.gpr[2] = io_error;
        return;
      }
      auto bits = static_cast<std::uint64_t>(state.gpr[5]) |
                  static_cast<std::uint64_t>(state.gpr[6]) << 32U;
      const auto offset = std::bit_cast<std::int64_t>(bits);
      const auto view = implementation.file_views.find(
          static_cast<int>(state.gpr[4]));
      off_t result{-1};
      if (view == implementation.file_views.end()) {
        result = ::lseek(descriptor, offset, static_cast<int>(state.gpr[7]));
      } else {
        std::optional<std::uint64_t> origin;
        if (state.gpr[7] == SEEK_SET) {
          origin = 0U;
        } else if (state.gpr[7] == SEEK_END) {
          origin = view->second.size;
        } else if (state.gpr[7] == SEEK_CUR) {
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
      implementation.async_results[static_cast<int>(state.gpr[4])] =
          result < 0 ? static_cast<std::int64_t>(static_cast<std::int32_t>(io_error))
                     : result;
      state.gpr[2] = 0U;
      state.gpr[3] = 0U;
      return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
