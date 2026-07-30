#include "elf.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace psprecomp {
namespace {

std::uint16_t u16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset > data.size() || data.size() - offset < 2) {
        throw std::runtime_error("truncated ELF field");
    }
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(data[offset + 1]) << 8U;
}

std::uint32_t u32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset > data.size() || data.size() - offset < 4) {
        throw std::runtime_error("truncated ELF field");
    }
    return static_cast<std::uint32_t>(data[offset]) |
           static_cast<std::uint32_t>(data[offset + 1]) << 8U |
           static_cast<std::uint32_t>(data[offset + 2]) << 16U |
           static_cast<std::uint32_t>(data[offset + 3]) << 24U;
}

std::string string_at(const std::vector<std::uint8_t>& table,
                      std::uint32_t offset) {
    if (offset >= table.size()) {
        throw std::runtime_error("invalid ELF section name offset");
    }
    std::string result;
    for (auto i = static_cast<std::size_t>(offset); i < table.size(); ++i) {
        if (table[i] == 0) {
            return result;
        }
        result.push_back(static_cast<char>(table[i]));
    }
    throw std::runtime_error("unterminated ELF section name");
}

struct SectionHeader {
    std::uint32_t name{};
    std::uint32_t type{};
    std::uint32_t flags{};
    std::uint32_t address{};
    std::uint32_t offset{};
    std::uint32_t size{};
    std::uint32_t info{};
};

} // namespace

std::uint32_t ElfImage::memory_size() const {
    std::uint32_t size = 0;
    for (const auto& segment : load_segments) {
        if (segment.address > std::numeric_limits<std::uint32_t>::max() -
                                  segment.memory_size) {
            throw std::runtime_error("load segment address overflow");
        }
        size = std::max(size, segment.address + segment.memory_size);
    }
    return size;
}

std::vector<std::uint8_t> ElfImage::memory_image() const {
    std::vector<std::uint8_t> image(memory_size());
    for (const auto& segment : load_segments) {
        if (segment.address > image.size() ||
            segment.bytes.size() > image.size() - segment.address) {
            throw std::runtime_error("load segment is outside memory image");
        }
        std::copy(segment.bytes.begin(), segment.bytes.end(),
                  image.begin() + segment.address);
    }
    return image;
}

bool CodeMap::contains(std::uint32_t address) const {
    const auto after = std::upper_bound(
        excluded_ranges.begin(), excluded_ranges.end(), address,
        [](std::uint32_t value, const AddressRange& range) {
            return value < range.begin;
        });
    if (after == excluded_ranges.begin()) {
        return true;
    }
    const auto& candidate = *std::prev(after);
    return address < candidate.begin || address >= candidate.end;
}

ElfImage load_elf(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("cannot open input: " + path.string());
    }
    const auto end = stream.tellg();
    if (end < 0) {
        throw std::runtime_error("cannot determine input size");
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    stream.seekg(0);
    if (!data.empty()) {
        stream.read(reinterpret_cast<char*>(data.data()),
                    static_cast<std::streamsize>(data.size()));
    }
    if (!stream || data.size() < 52) {
        throw std::runtime_error("input is not a complete ELF32 file");
    }
    if (data[0] != 0x7f || data[1] != 'E' || data[2] != 'L' ||
        data[3] != 'F') {
        throw std::runtime_error("input is not an ELF file");
    }
    if (data[4] != 1 || data[5] != 1) {
        throw std::runtime_error("only little-endian ELF32 is supported");
    }
    if (u16(data, 18) != 8) {
        throw std::runtime_error("ELF machine is not MIPS");
    }

    ElfImage image;
    image.entry = u32(data, 24);

    const auto program_offset = u32(data, 28);
    const auto program_entry_size = u16(data, 42);
    const auto program_count = u16(data, 44);
    const auto program_table_size = static_cast<std::size_t>(program_count) *
                                    program_entry_size;
    if ((program_count != 0 && program_entry_size < 32) ||
        program_table_size > data.size() ||
        program_offset > data.size() - program_table_size) {
        throw std::runtime_error("invalid ELF program table");
    }
    constexpr std::uint32_t pt_load = 1;
    for (std::uint16_t i = 0; i < program_count; ++i) {
        const auto base = static_cast<std::size_t>(program_offset) +
                          static_cast<std::size_t>(i) * program_entry_size;
        if (u32(data, base) != pt_load) {
            continue;
        }
        const auto file_offset = u32(data, base + 4);
        const auto address = u32(data, base + 8);
        const auto file_size = u32(data, base + 16);
        const auto memory_size = u32(data, base + 20);
        const auto flags = u32(data, base + 24);
        if (file_size > memory_size || file_offset > data.size() ||
            file_size > data.size() - file_offset) {
            throw std::runtime_error("invalid ELF load segment");
        }
        image.load_segments.push_back(
            {static_cast<std::uint8_t>(i), address, memory_size, flags,
             std::vector<std::uint8_t>(data.begin() + file_offset,
                                       data.begin() + file_offset + file_size)});
    }

    const auto section_offset = u32(data, 32);
    const auto section_entry_size = u16(data, 46);
    const auto section_count = u16(data, 48);
    const auto name_table_index = u16(data, 50);
    if (section_entry_size < 40 || name_table_index >= section_count) {
        throw std::runtime_error("invalid ELF section table");
    }
    const auto section_table_size = static_cast<std::size_t>(section_count) *
                                    section_entry_size;
    if (section_table_size > data.size() ||
        section_offset > data.size() - section_table_size) {
        throw std::runtime_error("ELF section table is outside the file");
    }

    std::vector<SectionHeader> headers;
    headers.reserve(section_count);
    for (std::uint16_t i = 0; i < section_count; ++i) {
        const auto base = static_cast<std::size_t>(section_offset) +
                          static_cast<std::size_t>(i) * section_entry_size;
        headers.push_back({u32(data, base), u32(data, base + 4),
                           u32(data, base + 8), u32(data, base + 12),
                           u32(data, base + 16), u32(data, base + 20),
                           u32(data, base + 28)});
    }

    const auto& string_header = headers[name_table_index];
    if (string_header.offset > data.size() ||
        string_header.size > data.size() - string_header.offset) {
        throw std::runtime_error("ELF section-name table is outside the file");
    }
    const std::vector<std::uint8_t> names(
        data.begin() + string_header.offset,
        data.begin() + string_header.offset + string_header.size);

    constexpr std::uint32_t sht_progbits = 1;
    constexpr std::uint32_t shf_execinstr = 4;
    for (const auto& header : headers) {
        if (header.type != sht_progbits ||
            (header.flags & shf_execinstr) == 0 || header.size == 0) {
            continue;
        }
        if (header.offset > data.size() ||
            header.size > data.size() - header.offset) {
            throw std::runtime_error("executable section is outside the file");
        }
        if ((header.address & 3U) != 0U || (header.size & 3U) != 0U) {
            throw std::runtime_error("executable section is not word-aligned");
        }
        image.executable_sections.push_back(
            {string_at(names, header.name), header.address,
             std::vector<std::uint8_t>(data.begin() + header.offset,
                                       data.begin() + header.offset +
                                           header.size)});
    }
    if (image.executable_sections.empty()) {
        throw std::runtime_error("ELF has no executable code sections");
    }


    constexpr std::uint32_t sht_psprel = 0x700000a0U;
    for (const auto& header : headers) {
        if (header.type != sht_psprel) {
            continue;
        }
        if ((header.size & 7U) != 0U || header.offset > data.size() ||
            header.size > data.size() - header.offset) {
            throw std::runtime_error("invalid PSP relocation section");
        }
        for (std::uint32_t offset = 0; offset < header.size; offset += 8) {
            const auto relocation_offset = u32(data, header.offset + offset);
            const auto info = u32(data, header.offset + offset + 4);
            image.relocations.push_back(
                {relocation_offset, static_cast<std::uint8_t>(info & 15U),
                 static_cast<std::uint8_t>((info >> 8U) & 0xffU),
                 static_cast<std::uint8_t>((info >> 16U) & 0xffU)});
        }
    }

    const auto flat_image = image.memory_image();
    const auto image_u32 = [&](std::uint32_t address) {
        return u32(flat_image, address);
    };
    const auto image_string = [&](std::uint32_t address) {
        if (address >= flat_image.size()) {
            throw std::runtime_error("PSP import string is outside load image");
        }
        std::string result;
        for (auto i = static_cast<std::size_t>(address); i < flat_image.size(); ++i) {
            if (flat_image[i] == 0) {
                return result;
            }
            result.push_back(static_cast<char>(flat_image[i]));
            if (result.size() > 255) {
                throw std::runtime_error("PSP import string is too long");
            }
        }
        throw std::runtime_error("unterminated PSP import string");
    };
    for (const auto& header : headers) {
        if (string_at(names, header.name) != ".rodata.sceModuleInfo" ||
            header.size < 0x34) {
            continue;
        }
        const auto stub_begin = image_u32(header.address + 0x2c);
        const auto stub_end = image_u32(header.address + 0x30);
        if (stub_begin > stub_end || stub_end > flat_image.size()) {
            throw std::runtime_error("invalid PSP module import-table range");
        }
        auto cursor = stub_begin;
        while (cursor < stub_end) {
            if (cursor > flat_image.size() || flat_image.size() - cursor < 20) {
                throw std::runtime_error("truncated PSP import table");
            }
            const auto library_address = image_u32(cursor);
            const auto length_words = flat_image[cursor + 8];
            const auto function_count = u16(flat_image, cursor + 10);
            const auto nid_table = image_u32(cursor + 12);
            const auto stub_table = image_u32(cursor + 16);
            if (length_words < 5 ||
                static_cast<std::uint32_t>(length_words) * 4U > stub_end - cursor) {
                throw std::runtime_error("invalid PSP import-table length");
            }
            const auto library = image_string(library_address);
            for (std::uint16_t i = 0; i < function_count; ++i) {
                image.imports.push_back(
                    {library, image_u32(nid_table + i * 4U),
                     stub_table + static_cast<std::uint32_t>(i) * 8U});
            }
            cursor += static_cast<std::uint32_t>(length_words) * 4U;
        }
    }
    return image;
}

CodeMap load_code_map(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("cannot open code map: " + path.string());
    }

    CodeMap map;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(stream, line)) {
        ++line_number;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string kind;
        std::string first;
        std::string second;
        fields >> kind >> first;
        const auto parse_address = [&](const std::string& value) {
            std::size_t consumed = 0;
            const auto parsed = std::stoul(value, &consumed, 0);
            if (consumed != value.size() ||
                parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error("invalid address in code map line " +
                                         std::to_string(line_number));
            }
            return static_cast<std::uint32_t>(parsed);
        };
        if (kind == "entry" && !first.empty()) {
            map.entry = parse_address(first);
        } else if (kind == "function" && !first.empty()) {
            const auto address = parse_address(first);
            map.function_starts.push_back(address);
            std::string name;
            if (fields >> name) {
                map.function_symbols.push_back({address, std::move(name)});
            }
        } else if (kind == "exclude" && !first.empty() && fields >> second) {
            const auto begin = parse_address(first);
            const auto end = parse_address(second);
            if (begin >= end || (begin & 3U) != 0U || (end & 3U) != 0U) {
                throw std::runtime_error("invalid excluded range in code map line " +
                                         std::to_string(line_number));
            }
            map.excluded_ranges.push_back({begin, end});
        } else {
            throw std::runtime_error("invalid code map line " +
                                     std::to_string(line_number));
        }
    }
    if (!stream.eof()) {
        throw std::runtime_error("failed while reading code map: " +
                                 path.string());
    }
    std::sort(map.function_starts.begin(), map.function_starts.end());
    std::sort(map.function_symbols.begin(), map.function_symbols.end(),
              [](const FunctionSymbol& left, const FunctionSymbol& right) {
                  return left.address < right.address;
              });
    std::sort(map.excluded_ranges.begin(), map.excluded_ranges.end(),
              [](const AddressRange& left, const AddressRange& right) {
                  return left.begin < right.begin;
              });
    return map;
}

const std::string* CodeMap::symbol_at(std::uint32_t address) const {
    const auto found = std::lower_bound(
        function_symbols.begin(), function_symbols.end(), address,
        [](const FunctionSymbol& symbol, std::uint32_t value) {
            return symbol.address < value;
        });
    return found != function_symbols.end() && found->address == address
               ? &found->name
               : nullptr;
}

} // namespace psprecomp
