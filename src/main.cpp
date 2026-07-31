#include "elf.hpp"
#include "emitter.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void usage(const char* program) {
    std::cerr << "Usage:\n"
              << "  " << program
              << " <input.elf> -o <output.cpp> [--code-map <map>]\n"
              << "  " << program
              << " <input.elf> --analyze [--code-map <map>]\n";
    std::cerr << "  " << program
              << " <input.elf> --output-dir <directory> [--code-map <map>]\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        usage(argv[0]);
        return 2;
    }
    try {
        std::filesystem::path output;
        std::filesystem::path map_path;
        std::filesystem::path output_directory;
        bool analyze = false;
        for (int i = 2; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--analyze") {
                analyze = true;
            } else if ((argument == "-o" || argument == "--code-map" ||
                        argument == "--output-dir") &&
                       i + 1 < argc) {
                if (argument == "-o") {
                    output = argv[++i];
                } else if (argument == "--code-map") {
                    map_path = argv[++i];
                } else {
                    output_directory = argv[++i];
                }
            } else {
                usage(argv[0]);
                return 2;
            }
        }
        const auto output_modes = static_cast<int>(analyze) +
                                  static_cast<int>(!output.empty()) +
                                  static_cast<int>(!output_directory.empty());
        if (output_modes != 1) {
            usage(argv[0]);
            return 2;
        }
        const auto image = psprecomp::load_elf(argv[1]);
        const auto code_map = map_path.empty()
                                  ? psprecomp::CodeMap{}
                                  : psprecomp::load_code_map(map_path);
        const auto* code_map_ptr = map_path.empty() ? nullptr : &code_map;
        if (analyze) {
            std::cout << "load_segments=" << image.load_segments.size() << '\n'
                      << "memory_size=" << image.memory_size() << '\n'
                      << "relocations=" << image.relocations.size() << '\n'
                      << "imports=" << image.imports.size() << '\n';
            return psprecomp::analyze_coverage(image, code_map_ptr, std::cout)
                       ? 0
                       : 1;
        }
        if (!output_directory.empty()) {
            psprecomp::emit_project(image, output_directory, code_map_ptr);
            std::cout << "Generated project: " << output_directory.string()
                      << " (" << image.memory_size() << " guest bytes, "
                      << image.relocations.size() << " relocations, "
                      << image.imports.size() << " imports)\n";
            return 0;
        }
        psprecomp::emit_cpp(image, output, code_map_ptr);
        std::cout << "Translated " << image.executable_sections.size()
                  << " executable section(s), entry 0x" << std::hex
                  << image.entry << " -> " << output.string() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "psprecomp: " << error.what() << '\n';
        return 1;
    }
}
