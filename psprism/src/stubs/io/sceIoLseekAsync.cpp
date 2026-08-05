void sceIoLseekAsync(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
      const auto descriptor =
          implementation.descriptor(static_cast<int>(state.gpr[4]));
      if (descriptor < 0)
        return;
      auto bits = static_cast<std::uint64_t>(state.gpr[5]) |
                  static_cast<std::uint64_t>(state.gpr[6]) << 32U;
      const auto base = implementation.file_bases.find(
          static_cast<int>(state.gpr[4]));
      if (base != implementation.file_bases.end() && state.gpr[7] == SEEK_SET)
        bits += base->second;
      auto result = ::lseek(descriptor, static_cast<std::int64_t>(bits),
                            static_cast<int>(state.gpr[7]));
      if (result >= 0 && base != implementation.file_bases.end())
        result -= static_cast<off_t>(base->second);
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
