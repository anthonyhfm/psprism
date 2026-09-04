#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace refract::host_file {

using Offset = std::int64_t;

inline int open(const std::filesystem::path& path, int flags, int mode) {
#if defined(_WIN32)
  static_cast<void>(mode);
  int descriptor{-1};
  const auto error = ::_wsopen_s(&descriptor, path.c_str(),
                                 flags | _O_BINARY, _SH_DENYNO,
                                 _S_IREAD | _S_IWRITE);
  return error == 0 ? descriptor : -1;
#else
  return ::open(path.c_str(), flags, static_cast<mode_t>(mode));
#endif
}

inline Offset seek(int descriptor, Offset offset, int origin) {
#if defined(_WIN32)
  return ::_lseeki64(descriptor, offset, origin);
#else
  return static_cast<Offset>(
      ::lseek(descriptor, static_cast<off_t>(offset), origin));
#endif
}

inline std::ptrdiff_t read(int descriptor, void* destination,
                           std::size_t size) {
#if defined(_WIN32)
  const auto count = static_cast<unsigned int>(std::min<std::size_t>(
      size, std::numeric_limits<unsigned int>::max()));
  return ::_read(descriptor, destination, count);
#else
  return ::read(descriptor, destination, size);
#endif
}

inline std::ptrdiff_t write(int descriptor, const void* source,
                            std::size_t size) {
#if defined(_WIN32)
  const auto count = static_cast<unsigned int>(std::min<std::size_t>(
      size, std::numeric_limits<unsigned int>::max()));
  return ::_write(descriptor, source, count);
#else
  return ::write(descriptor, source, size);
#endif
}

inline int close(int descriptor) {
#if defined(_WIN32)
  return ::_close(descriptor);
#else
  return ::close(descriptor);
#endif
}

} // namespace refract::host_file
