#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace psprecomp {

// A single file substitution to apply while rebuilding a PSP disc image.
// `iso_path` addresses the file inside the ISO 9660 tree (e.g.
// "PSP_GAME/SYSDIR/EBOOT.BIN"); `local_path` is the replacement content on
// disk.
struct IsoPatchReplacement {
  std::string iso_path;
  std::filesystem::path local_path;
};

// Rebuilds `output` from `original` while substituting the content of every
// entry in `replacements`.
//
// Unlike a full ISO 9660 repack (mkisofs/xorriso/hdiutil), this preserves the
// logical block address (LBA) of every file that is *not* being replaced.
// Several PSP games (observed with Need for Speed: Most Wanted) resolve
// large assets through raw sector addressing baked into their own code
// (`disc0:/sce_lbnXXXXX_sizeYYY`), bypassing the filesystem entirely. A
// from-scratch repack silently relocates that data, so those games load
// garbage/corrupted modules and crash or hang instead of booting.
//
// When a replacement fits within the original file's already-allocated
// sectors it is written in place, keeping its LBA unchanged. When it does
// not fit, the new data is appended past the end of the image (sector
// aligned) and only that file's directory record (extent + size, in both
// the little- and big-endian ISO 9660 fields) is patched to point at the
// new location. Every other file keeps its original LBA untouched.
void patch_iso_preserving_layout(
    const std::filesystem::path &original, const std::filesystem::path &output,
    const std::vector<IsoPatchReplacement> &replacements);

} // namespace psprecomp
