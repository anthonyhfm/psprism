    {
        std::map<std::pair<std::string, std::uint32_t>,
                 std::vector<const PlatformImport*>> import_libraries;
        for (const auto& import : platform_imports) {
            import_libraries[{import.source->library,
                              import.source->library_flags}]
                .push_back(&import);
        }
        std::ofstream stream(directory / "imports.S",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create generated PSP imports");
        }
        stream << "# Generated from the original PRX import metadata.\n"
                  ".set noreorder\n";
        std::size_t library_index = 0;
        for (const auto& [key, imports] : import_libraries) {
            if (imports.size() > std::numeric_limits<std::uint16_t>::max()) {
                throw std::runtime_error("PSP import library has too many stubs");
            }
            const auto label = "psprecomp_library_" +
                               std::to_string(library_index++);
            stream << ".section .rodata.sceResident, \"a\"\n"
                      ".word 0\n."
                   << label << "_name:\n.asciz \""
                   << cpp_string(key.first)
                   << "\"\n.align 2\n"
                      ".section .lib.stub, \"a\", @progbits\n.word ."
                   << label << "_name\n.word " << hex_address(key.second)
                   << "\n.word "
                   << hex_address((static_cast<std::uint32_t>(imports.size())
                                   << 16U) |
                                  5U)
                   << "\n.word ." << label << "_nids\n.word ." << label
                   << "_stubs\n"
                      ".section .rodata.sceNid, \"a\"\n."
                   << label << "_nids:\n";
            for (const auto* import : imports) {
                stream << ".word " << hex_address(import->source->nid) << "\n";
            }
            stream << ".section .sceStub.text, \"ax\", @progbits\n."
                   << label << "_stubs:\n";
            for (const auto* import : imports) {
                stream << ".globl " << import->psp_symbol << "\n.type "
                       << import->psp_symbol << ", @function\n.ent "
                       << import->psp_symbol << ", 0\n"
                       << import->psp_symbol << ":\n"
                          "jr $ra\nnop\n.end "
                       << import->psp_symbol << "\n.size "
                       << import->psp_symbol << ", .-" << import->psp_symbol
                       << "\n";
            }
        }
    }
    {
        std::ofstream stream(directory / "relocations.bin",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create relocation image");
        }
        for (const auto& relocation : image.relocations) {
            const std::uint8_t record[8] = {
                static_cast<std::uint8_t>(relocation.offset),
                static_cast<std::uint8_t>(relocation.offset >> 8U),
                static_cast<std::uint8_t>(relocation.offset >> 16U),
                static_cast<std::uint8_t>(relocation.offset >> 24U),
                relocation.type,
                relocation.patch_segment,
                relocation.target_segment,
                0,
            };
            stream.write(reinterpret_cast<const char*>(record), sizeof(record));
        }
    }
    {
        std::ofstream stream(directory / "native_relocations.bin",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error(
                "cannot create compact native relocation image");
        }
        stream.write(
            reinterpret_cast<const char*>(native_relocation_bytes.data()),
            static_cast<std::streamsize>(native_relocation_bytes.size()));
    }
    {
        std::ofstream stream(directory / "imports.tsv",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create import manifest");
        }
        stream << "stub_address\tnid\tlibrary\tsymbol\n";
        for (const auto& import : image.imports) {
            stream << "0x" << std::hex << std::setfill('0') << std::setw(8)
                   << import.stub_address << "\t0x" << std::setw(8)
                   << import.nid << "\t" << import.library << "\t";
            stream << import_symbol(import, code_map);
            stream << "\n";
        }
    }
    {
        std::ofstream stream(directory / "generated_sources.cmake",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create generated source manifest");
        }
        stream << "set(PSPRECOMP_GENERATED_SOURCES\n";
        for (const auto& name : source_names) {
            stream << "  ${CMAKE_CURRENT_LIST_DIR}/" << name << "\n";
        }
        stream << ")\n";
    }
    {
        std::ofstream stream(directory / "Makefile",
                             std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot create generated PSP Makefile");
        }
        const auto psp_sources =
            std::filesystem::relative(platform_directory / "psp", directory)
                .generic_string();
        const auto refract_sources =
            std::filesystem::relative(
                platform_directory.parent_path() / "refract" /
                    "src",
                directory)
                .generic_string();
        const auto project_root =
            std::filesystem::relative(platform_directory.parent_path(),
                                      directory)
                .generic_string();
        stream << "PSPSDK := $(shell psp-config --pspsdk-path)\n"
                  "TARGET = "
               << make_value(options.target_name)
               << "\n"
                  "PSPRECOMP_PROJECT_ROOT ?= "
               << make_value(project_root)
               << "\n"
                  "VPATH = "
               << make_value(psp_sources)
               << " "
               << make_value(refract_sources)
               << "\n";
        if (psp_overlay_mode) {
            stream << "OBJS = main.o imports.o psp_entry.o";
            if (!psp_overlays.empty()) {
                stream << " overlay.o psp_overlays.o";
                for (const auto& overlay : psp_overlays) {
                    std::ostringstream object;
                    object << " func_" << std::hex << std::setfill('0')
                           << std::setw(8) << overlay.start << ".o";
                    stream << object.str();
                }
            }
            stream << " native_guest_image.o\n";
        } else {
            stream << "OBJS = main.o platform.o imports.o dispatch.o "
                      "psp_sdk_stubs.o";
            for (const auto& name : source_names) {
                if (name.rfind("shard_", 0) == 0 ||
                    name.rfind("func_", 0) == 0) {
                    stream << " " << name.substr(0, name.size() - 4) << ".o";
                }
            }
            stream << " guest_image.o guest_relocations.o\n";
        }
        stream << "BUILD_PRX = 1\n"
                  "EXTRA_TARGETS = EBOOT.PBP\n"
                  "PSP_EBOOT_TITLE = "
               << make_value(options.display_name)
               << "\n"
                  "PSPRECOMP_INCLUDE ?= "
               << make_value(options.include_path)
               << "\n"
                  "CFLAGS = -O2 -G0 -Wall -I$(PSPRECOMP_INCLUDE) "
                  "-I$(PSPRECOMP_PROJECT_ROOT) "
                  "-I$(PSPRECOMP_PROJECT_ROOT)/refract/include -I.\n"
                  "CXXFLAGS = $(CFLAGS) -std=c++20 -fno-exceptions -fno-rtti\n"
                  "ASFLAGS = $(CFLAGS)\n"
                  "LIBS = -lpsputility -lpspmp3 -lpspdmac -lpspmpeg "
                  "-lpspmpegbase -lpspatrac3 "
                  "-lpspsascore -lpspaudio -lpspctrl -lpspdisplay -lpspge "
                  "-lpsppower -lpspusb -lpspumd -lpspwlan "
                  "-lpspnet_adhocctl -lpspnet_adhocmatching -lpspnet_adhoc "
                  "-lpspnet\n\n";
        for (const auto& overlay : psp_overlays) {
            stream << "func_" << std::hex << std::setfill('0')
                   << std::setw(8) << overlay.start
                   << ".o: CXXFLAGS += -DPSPRECOMP_PSP_OVERLAY\n";
        }
        stream << "\ninclude $(PSPSDK)/lib/build.mak\n\n"
                  "guest_image.o: guest_image.bin\n"
                  "\tbin2o -n -i -a 64 $< $@ guest_image\n\n"
                  "native_guest_image.o: native_guest_image.bin\n"
                  "\tbin2o -n -i -a 64 $< $@ native_guest_image\n\n"
                  "guest_relocations.o: relocations.bin\n"
                  "\tbin2o -n -i -a 4 $< $@ guest_relocations\n";
    }
}
