    std::map<std::uint32_t, std::vector<EmittedInstruction>> shards;
    std::set<std::uint32_t> delay_slots;
    const auto relocated = relocated_words(image);
    const auto jump_bases = jump_relocation_bases(image);
    for (const auto& section : image.executable_sections) {
        for (std::size_t offset = 0; offset < section.bytes.size();
             offset += 4) {
            const auto pc =
                section.address + static_cast<std::uint32_t>(offset);
            const auto instruction = word_at(section, offset);
            shards[pc / shard_size].push_back(
                {pc, instruction,
                 emit_instruction(pc, instruction, relocated.contains(pc),
                                  image.preferred_base),
                 direct_control_target(pc, instruction, jump_bases,
                                       image.preferred_base)});
            if (starts_delayed_branch(instruction)) {
                delay_slots.insert(pc + 4U);
            }
        }
    }

    std::set<std::uint32_t> import_stubs;
    for (const auto& import : image.imports) {
        import_stubs.insert(import.stub_address);
    }

    // Function discovery partitions the binary into distinct Guest functions.
    // Keeping each Guest function as a separate C++ function gives the host
    // compiler a tractable control-flow graph and lets static J/JAL edges stay
    // on the native call stack. Each discovered Guest function is emitted into
    // its own well-named source file so generated codebases remain easy to navigate and patch.
    const auto discovered_funcs = discover_functions(image, code_map);
    std::vector<std::uint32_t> mapped_function_starts;
    std::map<std::uint32_t, std::string> function_filenames;
    std::map<std::uint32_t, std::string> function_symbols;
    for (const auto& df : discovered_funcs) {
        mapped_function_starts.push_back(df.start);
        function_filenames[df.start] = df.filename;
        if (!df.symbol.empty()) {
            function_symbols[df.start] = df.symbol;
        }
    }
    std::sort(mapped_function_starts.begin(), mapped_function_starts.end());
    mapped_function_starts.erase(std::unique(mapped_function_starts.begin(),
                                             mapped_function_starts.end()),
                                 mapped_function_starts.end());

    std::map<std::uint32_t, std::vector<EmittedInstruction>> functions;
    for (const auto& [unused_shard, instructions] : shards) {
        static_cast<void>(unused_shard);
        for (const auto& emitted : instructions) {
            const auto after =
                std::upper_bound(mapped_function_starts.begin(),
                                 mapped_function_starts.end(), emitted.pc);
            if (after == mapped_function_starts.begin()) {
                continue;
            }
            const auto owner = *std::prev(after);
            if (!import_stubs.contains(owner)) {
                functions[owner].push_back(emitted);
            }
        }
    }
    const bool function_mode = true;

    const auto function_owner =
        [&](std::uint32_t address) -> std::optional<std::uint32_t> {
        if (!function_mode) {
            return std::nullopt;
        }
        const auto after =
            std::upper_bound(mapped_function_starts.begin(),
                             mapped_function_starts.end(), address);
        if (after == mapped_function_starts.begin()) {
            return std::nullopt;
        }
        const auto owner = *std::prev(after);
        return functions.contains(owner) ? std::optional(owner) : std::nullopt;
    };

    const auto entry = code_map != nullptr ? code_map->entry : image.entry;
    const auto block_entries =
        potential_block_entries(image, code_map, entry, shard_size);

    struct PspOverlay {
        std::uint32_t start{};
        std::uint32_t end{};
        std::size_t index{};
        bool uses_vfpu{};
        std::size_t entry_route{};
    };
    struct PspOverlayRoute {
        std::size_t overlay_index{};
        std::uint32_t entry{};
        std::uint32_t link_register{};
        std::size_t index{};
        bool resume{};
        bool uses_vfpu{};
    };
    std::vector<PspOverlay> psp_overlays;
    std::vector<PspOverlayRoute> psp_overlay_routes;
    if (code_map != nullptr && !code_map->overlay_starts.empty()) {
        if (image.preferred_base != 0U) {
            throw std::runtime_error(
                "PSP overlays require a relocatable PRX image");
        }
        for (const auto start : code_map->overlay_starts) {
            const auto found = functions.find(start);
            if (found == functions.end() || found->second.size() < 2U) {
                throw std::runtime_error(
                    "PSP overlay is not a mapped function with at least "
                    "two instructions: " +
                    hex_address(start));
            }
            if (relocated.contains(start) || relocated.contains(start + 4U)) {
                throw std::runtime_error(
                    "PSP overlay entry detour would hide a relocated "
                    "instruction: " +
                    hex_address(start));
            }
            const auto end = found->second.back().pc + 4U;
            for (const auto& emitted : found->second) {
                const auto op = emitted.instruction >> 26U;
                const auto function = emitted.instruction & 0x3fU;
                const auto rs = (emitted.instruction >> 21U) & 0x1fU;
                const bool dynamic_jump =
                    op == 0U && function == 0x08U && rs != 31U;
                if (dynamic_jump) {
                    throw std::runtime_error(
                        "PSP overlay contains a dynamic non-return jump: " +
                        hex_address(start));
                }
            }
            const bool uses_vfpu = std::any_of(
                found->second.begin(), found->second.end(),
                [](const EmittedInstruction& emitted) {
                    return vfpu_opcode_supported(emitted.instruction);
                });
            psp_overlays.push_back(
                {start, end, psp_overlays.size(), uses_vfpu, 0U});
        }
        for (auto& overlay : psp_overlays) {
            overlay.entry_route = psp_overlay_routes.size();
            psp_overlay_routes.push_back(
                {overlay.index, overlay.start, 31U,
                 psp_overlay_routes.size(), false, overlay.uses_vfpu});
            std::map<std::uint32_t, std::uint32_t> continuations;
            for (const auto& emitted : functions.at(overlay.start)) {
                const auto op = emitted.instruction >> 26U;
                const auto function = emitted.instruction & 0x3fU;
                const auto rt = (emitted.instruction >> 16U) & 0x1fU;
                const auto rd = (emitted.instruction >> 11U) & 0x1fU;
                const bool links =
                    op == 0x03U ||
                    (op == 0U && function == 0x09U && rd != 0U) ||
                    (op == 0x01U && rt >= 0x10U && rt <= 0x13U);
                if (links) {
                    const auto link_register =
                        op == 0U ? rd : static_cast<std::uint32_t>(31U);
                    const auto [found, inserted] = continuations.emplace(
                        emitted.pc + 8U, link_register);
                    if (!inserted && found->second != link_register) {
                        throw std::runtime_error(
                            "PSP overlay continuation uses multiple link "
                            "registers: " +
                            hex_address(overlay.start));
                    }
                }
            }
            for (const auto& [continuation, link_register] : continuations) {
                if (continuation < overlay.start ||
                    continuation >= overlay.end) {
                    throw std::runtime_error(
                        "PSP overlay call continuation is outside its "
                        "mapped function: " +
                        hex_address(overlay.start));
                }
                psp_overlay_routes.push_back(
                    {overlay.index, continuation, link_register,
                     psp_overlay_routes.size(), true, overlay.uses_vfpu});
            }
        }
    }
    const bool psp_overlay_mode = !psp_overlays.empty();
