#include "../elf.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace psprecomp
{
    namespace
    {

        std::uint16_t u16(const std::vector<std::uint8_t> &data, std::size_t offset)
        {
            if (offset > data.size() || data.size() - offset < 2)
            {
                throw std::runtime_error("truncated ELF field");
            }
            return static_cast<std::uint16_t>(data[offset]) |
                   static_cast<std::uint16_t>(data[offset + 1]) << 8U;
        }

        std::uint32_t u32(const std::vector<std::uint8_t> &data, std::size_t offset)
        {
            if (offset > data.size() || data.size() - offset < 4)
            {
                throw std::runtime_error("truncated ELF field");
            }
            return static_cast<std::uint32_t>(data[offset]) |
                   static_cast<std::uint32_t>(data[offset + 1]) << 8U |
                   static_cast<std::uint32_t>(data[offset + 2]) << 16U |
                   static_cast<std::uint32_t>(data[offset + 3]) << 24U;
        }

        std::string string_at(const std::vector<std::uint8_t> &table,
                              std::uint32_t offset)
        {
            if (offset >= table.size())
            {
                throw std::runtime_error("invalid ELF section name offset");
            }
            std::string result;
            for (auto i = static_cast<std::size_t>(offset); i < table.size(); ++i)
            {
                if (table[i] == 0)
                {
                    return result;
                }
                result.push_back(static_cast<char>(table[i]));
            }
            throw std::runtime_error("unterminated ELF section name");
        }

        struct SectionHeader
        {
            std::uint32_t name{};
            std::uint32_t type{};
            std::uint32_t flags{};
            std::uint32_t address{};
            std::uint32_t offset{};
            std::uint32_t size{};
            std::uint32_t info{};
        };

        struct ProgramLoadHeader
        {
            std::uint32_t file_offset{};
            std::uint32_t address{};
            std::uint32_t physical_address{};
            std::uint32_t file_size{};
        };

    } // namespace
    ElfImage load_elf(const std::filesystem::path &path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            throw std::runtime_error("cannot open input: " + path.string());
        }
        const auto end = stream.tellg();
        if (end < 0)
        {
            throw std::runtime_error("cannot determine input size");
        }
        std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
        stream.seekg(0);
        if (!data.empty())
        {
            stream.read(reinterpret_cast<char *>(data.data()),
                        static_cast<std::streamsize>(data.size()));
        }
        if (!stream || data.size() < 52)
        {
            throw std::runtime_error("input is not a complete ELF32 file");
        }
        if (data[0] != 0x7f || data[1] != 'E' || data[2] != 'L' ||
            data[3] != 'F')
        {
            throw std::runtime_error("input is not an ELF file");
        }
        if (data[4] != 1 || data[5] != 1)
        {
            throw std::runtime_error("only little-endian ELF32 is supported");
        }
        if (u16(data, 18) != 8)
        {
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
            program_offset > data.size() - program_table_size)
        {
            throw std::runtime_error("invalid ELF program table");
        }
        constexpr std::uint32_t pt_load = 1;
        std::vector<ProgramLoadHeader> program_load_headers;
        for (std::uint16_t i = 0; i < program_count; ++i)
        {
            const auto base = static_cast<std::size_t>(program_offset) +
                              static_cast<std::size_t>(i) * program_entry_size;
            if (u32(data, base) != pt_load)
            {
                continue;
            }
            const auto file_offset = u32(data, base + 4);
            const auto address = u32(data, base + 8);
            const auto physical_address = u32(data, base + 12);
            const auto file_size = u32(data, base + 16);
            const auto memory_size = u32(data, base + 20);
            const auto flags = u32(data, base + 24);
            if (file_size > memory_size || file_offset > data.size() ||
                file_size > data.size() - file_offset)
            {
                throw std::runtime_error("invalid ELF load segment");
            }
            image.load_segments.push_back(
                {static_cast<std::uint8_t>(i), address, memory_size, flags,
                 std::vector<std::uint8_t>(data.begin() + file_offset,
                                           data.begin() + file_offset + file_size)});
            program_load_headers.push_back(
                {file_offset, address, physical_address, file_size});
        }

        constexpr std::uint32_t psp_user_base = 0x08000000U;
        constexpr std::uint32_t psp_user_end = 0x0c000000U;
        if (!image.load_segments.empty() &&
            std::all_of(image.load_segments.begin(), image.load_segments.end(),
                        [](const LoadSegment &segment)
                        {
                            return segment.address >= psp_user_base &&
                                   segment.address < psp_user_end &&
                                   segment.memory_size <= psp_user_end - segment.address;
                        }))
        {
            image.preferred_base = psp_user_base;
            if (image.entry < image.preferred_base)
            {
                throw std::runtime_error("fixed PSP ELF entry is below user memory");
            }
            image.entry -= image.preferred_base;
            for (auto &segment : image.load_segments)
            {
                segment.address -= image.preferred_base;
            }
        }

        const auto section_offset = u32(data, 32);
        const auto section_entry_size = u16(data, 46);
        const auto section_count = u16(data, 48);
        const auto name_table_index = u16(data, 50);
        if (section_entry_size < 40 || name_table_index >= section_count)
        {
            throw std::runtime_error("invalid ELF section table");
        }
        const auto section_table_size = static_cast<std::size_t>(section_count) *
                                        section_entry_size;
        if (section_table_size > data.size() ||
            section_offset > data.size() - section_table_size)
        {
            throw std::runtime_error("ELF section table is outside the file");
        }

        std::vector<SectionHeader> headers;
        headers.reserve(section_count);
        for (std::uint16_t i = 0; i < section_count; ++i)
        {
            const auto base = static_cast<std::size_t>(section_offset) +
                              static_cast<std::size_t>(i) * section_entry_size;
            headers.push_back({u32(data, base), u32(data, base + 4),
                               u32(data, base + 8), u32(data, base + 12),
                               u32(data, base + 16), u32(data, base + 20),
                               u32(data, base + 28)});
        }

        const auto &string_header = headers[name_table_index];
        if (string_header.offset > data.size() ||
            string_header.size > data.size() - string_header.offset)
        {
            throw std::runtime_error("ELF section-name table is outside the file");
        }
        const std::vector<std::uint8_t> names(
            data.begin() + string_header.offset,
            data.begin() + string_header.offset + string_header.size);

        constexpr std::uint32_t sht_progbits = 1;
        constexpr std::uint32_t shf_execinstr = 4;
        for (const auto &header : headers)
        {
            if (header.type != sht_progbits ||
                (header.flags & shf_execinstr) == 0 ||
                header.size == 0)
            {
                continue;
            }
            if (header.offset > data.size() ||
                header.size > data.size() - header.offset)
            {
                throw std::runtime_error("executable section is outside the file");
            }
            if ((header.address & 3U) != 0U || (header.size & 3U) != 0U)
            {
                throw std::runtime_error("executable section is not word-aligned");
            }
            image.executable_sections.push_back(
                {string_at(names, header.name),
                 header.address - image.preferred_base,
                 std::vector<std::uint8_t>(data.begin() + header.offset,
                                           data.begin() + header.offset +
                                               header.size)});
        }
        if (image.executable_sections.empty())
        {
            throw std::runtime_error("ELF has no executable code sections");
        }

        constexpr std::uint32_t sht_psprel = 0x700000a0U;
        for (const auto &header : headers)
        {
            if (header.type != sht_psprel)
            {
                continue;
            }
            if ((header.size & 7U) != 0U || header.offset > data.size() ||
                header.size > data.size() - header.offset)
            {
                throw std::runtime_error("invalid PSP relocation section");
            }
            for (std::uint32_t offset = 0; offset < header.size; offset += 8)
            {
                const auto relocation_offset = u32(data, header.offset + offset);
                const auto info = u32(data, header.offset + offset + 4);
                image.relocations.push_back(
                    {relocation_offset, static_cast<std::uint8_t>(info & 15U),
                     static_cast<std::uint8_t>((info >> 8U) & 0xffU),
                     static_cast<std::uint8_t>((info >> 16U) & 0xffU)});
            }
        }

        const auto flat_image = image.memory_image();
        const auto image_offset = [&](std::uint32_t address)
        {
            if (address < image.preferred_base)
            {
                throw std::runtime_error("PSP address is below the image base");
            }
            return address - image.preferred_base;
        };
        const auto image_u32 = [&](std::uint32_t address)
        {
            return u32(flat_image, image_offset(address));
        };
        const auto image_string = [&](std::uint32_t address)
        {
            address = image_offset(address);
            if (address >= flat_image.size())
            {
                throw std::runtime_error("PSP import string is outside load image");
            }
            std::string result;
            for (auto i = static_cast<std::size_t>(address); i < flat_image.size(); ++i)
            {
                if (flat_image[i] == 0)
                {
                    return result;
                }
                result.push_back(static_cast<char>(flat_image[i]));
                if (result.size() > 255)
                {
                    throw std::runtime_error("PSP import string is too long");
                }
            }
            throw std::runtime_error("unterminated PSP import string");
        };
        std::optional<std::uint32_t> module_info_address;
        for (const auto &header : headers)
        {
            if (string_at(names, header.name) == ".rodata.sceModuleInfo" &&
                header.size >= 0x34)
            {
                module_info_address = header.address;
                break;
            }
        }

        // Stripped retail PRX files commonly retain the section table but erase
        // every section name.  For PSP ET_SCE_PRX files, the first load program
        // header's p_paddr is the file offset of SceModuleInfo.  Translate that
        // file offset back to its virtual address instead of relying on the
        // optional .rodata.sceModuleInfo name.
        constexpr std::uint16_t et_sce_prx = 0xffa0U;
        if (!module_info_address && u16(data, 16) == et_sce_prx &&
            !program_load_headers.empty())
        {
            const auto module_info_file_offset =
                program_load_headers.front().physical_address;
            for (const auto &segment : program_load_headers)
            {
                if (module_info_file_offset >= segment.file_offset &&
                    module_info_file_offset - segment.file_offset <=
                        segment.file_size &&
                    segment.file_size -
                            (module_info_file_offset - segment.file_offset) >=
                        0x34U)
                {
                    module_info_address =
                        segment.address +
                        (module_info_file_offset - segment.file_offset);
                    break;
                }
            }
        }

        if (module_info_address)
        {
            const auto address = *module_info_address;
            // gp_value lives at struct offset 0x20 (SceModuleInfo::gp_value).
            // Unlike stub_begin/stub_end, its raw stored value is only an
            // addend for a runtime R_MIPS_32 relocation (typically against the
            // small-data segment), so we must not resolve it here: just record
            // where the field lives so the generated runtime can read the
            // already-relocated value from guest memory after relocations run.
            image.gp_pointer_offset = image_offset(address + 0x20);
            image.gp_value_offset = image_u32(address + 0x20);
            for (const auto &relocation : image.relocations)
            {
                const auto patch = std::find_if(
                    image.load_segments.begin(), image.load_segments.end(),
                    [&](const LoadSegment &segment)
                    {
                        return segment.program_index == relocation.patch_segment;
                    });
                if (patch == image.load_segments.end() || relocation.type != 2U ||
                    patch->address + relocation.offset != image.gp_pointer_offset)
                {
                    continue;
                }
                const auto target = std::find_if(
                    image.load_segments.begin(), image.load_segments.end(),
                    [&](const LoadSegment &segment)
                    {
                        return segment.program_index == relocation.target_segment;
                    });
                if (target == image.load_segments.end())
                {
                    throw std::runtime_error("invalid PSP module gp relocation");
                }
                image.gp_value_offset += target->address;
                break;
            }
            const auto stub_begin = image_offset(image_u32(address + 0x2c));
            const auto stub_end = image_offset(image_u32(address + 0x30));
            if (stub_begin > stub_end || stub_end > flat_image.size())
            {
                throw std::runtime_error("invalid PSP module import-table range");
            }
            auto cursor = stub_begin;
            while (cursor < stub_end)
            {
                if (cursor > flat_image.size() || flat_image.size() - cursor < 20)
                {
                    throw std::runtime_error("truncated PSP import table");
                }
                const auto library_address = u32(flat_image, cursor);
                const auto library_flags = u32(flat_image, cursor + 4);
                const auto length_words = flat_image[cursor + 8];
                const auto function_count = u16(flat_image, cursor + 10);
                const auto nid_table = u32(flat_image, cursor + 12);
                const auto stub_table = u32(flat_image, cursor + 16);
                if (length_words < 5 ||
                    static_cast<std::uint32_t>(length_words) * 4U > stub_end - cursor)
                {
                    throw std::runtime_error("invalid PSP import-table length");
                }
                const auto library = image_string(library_address);
                for (std::uint16_t i = 0; i < function_count; ++i)
                {
                    image.imports.push_back(
                        {library, image_u32(nid_table + i * 4U),
                         image_offset(stub_table) +
                             static_cast<std::uint32_t>(i) * 8U,
                         library_flags});
                }
                cursor += static_cast<std::uint32_t>(length_words) * 4U;
            }
        }
        return image;
    }

} // namespace psprecomp
