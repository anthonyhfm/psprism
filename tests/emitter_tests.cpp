#include "elf.hpp"
#include "emitter.hpp"

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            std::cerr << "check failed: " #expression << " at " << __FILE__  \
                      << ':' << __LINE__ << '\n';                              \
            return 1;                                                          \
        }                                                                      \
    } while (false)

namespace {

void append_word(std::vector<std::uint8_t>& bytes, std::uint32_t word) {
    bytes.push_back(static_cast<std::uint8_t>(word));
    bytes.push_back(static_cast<std::uint8_t>(word >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(word >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(word >> 24U));
}

} // namespace

int main() {
    psprecomp::ElfImage image;
    psprecomp::ExecutableSection section{".text", 0x1000U, {}};
    append_word(section.bytes, 0x2402002aU); // addiu: native
    append_word(section.bytes, 0xd00380a3U); // vidt.q: runtime fallback
    append_word(section.bytes, 0x00400001U); // reserved SPECIAL
    append_word(section.bytes, 0xffffffffU); // excluded data
    append_word(section.bytes, 0x60000000U); // vadd.s: guarded native
    image.executable_sections.push_back(std::move(section));

    psprecomp::CodeMap map;
    map.excluded_ranges.push_back({0x100cU, 0x1010U});
    std::ostringstream report;
    CHECK(!psprecomp::analyze_coverage(image, &map, report));
    const auto text = report.str();
    CHECK(text.find("translated_words=1\n") != std::string::npos);
    CHECK(text.find("excluded_words=1\n") != std::string::npos);
    CHECK(text.find("fallback_words=1\n") != std::string::npos);
    CHECK(text.find("guarded_native_words=1\n") != std::string::npos);
    CHECK(text.find("guarded_native vadd count=1") != std::string::npos);
    CHECK(text.find("invalid_words=1\n") != std::string::npos);
    CHECK(text.find("fallback vfpu count=1") != std::string::npos);

    map.excluded_ranges = {{0x1008U, 0x1010U}};
    std::ostringstream clean_report;
    CHECK(psprecomp::analyze_coverage(image, &map, clean_report));
    CHECK(clean_report.str().find("invalid_words=0\n") != std::string::npos);

    const auto output = std::filesystem::temp_directory_path() /
                        ("psprism-vfpu-emitter-" +
                         std::to_string(std::chrono::steady_clock::now()
                                            .time_since_epoch()
                                            .count()) +
                         ".cpp");
    psprecomp::emit_cpp(image, output);
    std::ifstream generated_stream(output);
    const std::string generated((std::istreambuf_iterator<char>(generated_stream)),
                                std::istreambuf_iterator<char>());
    std::filesystem::remove(output);
    CHECK(generated.find("execute_vfpu_prefix_free<"
                         "VfpuStaticOperation::add, 1>") !=
          std::string::npos);
    CHECK(generated.find("vfpu_prefixes_identity(state)") !=
          std::string::npos);
    CHECK(generated.find("note_vfpu_helper_fallback(state)") !=
          std::string::npos);
}
