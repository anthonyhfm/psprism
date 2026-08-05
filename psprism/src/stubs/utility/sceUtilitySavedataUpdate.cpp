void sceUtilitySavedataUpdate(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  if (implementation.savedata_status == 2U &&
      !implementation.savedata_operation_complete) {
    if (implementation.savedata_dialog_presented) {
      const auto result = host::poll_dialog_result(
          implementation.savedata_dialog_id);
      if (!result) {
        state.gpr[2] = 0U;
        return;
      }
      if (auto* parameters = psprecomp::mapped_address(
              state, implementation.savedata_parameters, 128U)) {
        if (result->cancelled) {
          std::memcpy(parameters + 28U, &utility_cancelled,
                      sizeof(utility_cancelled));
          implementation.savedata_operation_complete = true;
          implementation.savedata_status = 3U;
          state.gpr[2] = 0U;
          return;
        }
        if (result->selected_item < implementation.savedata_names.size()) {
          std::memset(parameters + 76U, 0, 20U);
          const auto& selected = implementation.savedata_names[
              result->selected_item];
          std::memcpy(parameters + 76U, selected.data(),
                      std::min<std::size_t>(selected.size(), 19U));
        }
      }
    }
    implementation.savedata_operation_complete = true;
    implementation.savedata_status = 3U;
    if (auto* parameters = psprecomp::mapped_address(
            state, implementation.savedata_parameters, 1536U)) {
      const std::uint32_t success{};
      std::memcpy(parameters + 28U, &success, sizeof(success));
      std::uint32_t mode{};
      std::memcpy(&mode, parameters + 48U, sizeof(mode));
      char game_name[14]{};
      char save_name[21]{};
      char file_name[14]{};
      std::memcpy(game_name, parameters + 60U, 13U);
      std::memcpy(save_name, parameters + 76U, 20U);
      std::memcpy(file_name, parameters + 100U, 13U);
      const auto safe_component = [](const char* value) {
        return value[0] != '\0' && std::strcmp(value, ".") != 0 &&
               std::strcmp(value, "..") != 0 &&
               std::strchr(value, '/') == nullptr &&
               std::strchr(value, '\\') == nullptr;
      };
      const auto save_directory =
          implementation.configuration.writable_root / "PSP" / "SAVEDATA" /
          (std::string(game_name) + save_name);
      const auto save_file = save_directory / file_name;
      const bool valid_directory = safe_component(game_name) &&
                                   safe_component(save_name);
      const bool valid_path = valid_directory && safe_component(file_name);
      const bool load_mode = mode == 0U || mode == 2U || mode == 4U ||
                             mode == 15U || mode == 16U;
      const bool save_mode = mode == 1U || mode == 3U || mode == 5U ||
                             mode == 13U || mode == 14U || mode == 17U ||
                             mode == 18U;
      const bool delete_mode = mode == 6U || mode == 7U || mode == 9U ||
                               mode == 10U || mode == 19U || mode == 20U ||
                               mode == 21U;
      if (load_mode) {
        std::uint32_t data_address{};
        std::uint32_t buffer_size{};
        std::memcpy(&data_address, parameters + 116U,
                    sizeof(data_address));
        std::memcpy(&buffer_size, parameters + 120U,
                    sizeof(buffer_size));
        std::error_code file_error;
        const auto file_size = valid_path
                                   ? std::filesystem::file_size(save_file,
                                                                file_error)
                                   : 0U;
        auto* destination = psprecomp::mapped_address(
            state, data_address,
            static_cast<std::size_t>(std::min<std::uintmax_t>(
                file_size, static_cast<std::uintmax_t>(buffer_size))));
        if (!valid_path || file_error || destination == nullptr) {
          const std::uint32_t no_data =
              mode == 15U || mode == 16U ? 0x80110327U : 0x80110307U;
          std::memcpy(parameters + 28U, &no_data, sizeof(no_data));
        } else {
          const auto read_size = static_cast<std::uint32_t>(
              std::min<std::uintmax_t>(file_size, buffer_size));
          std::ifstream input(save_file, std::ios::binary);
          input.read(reinterpret_cast<char*>(destination), read_size);
          std::memcpy(parameters + 124U, &read_size, sizeof(read_size));
          if (!input) {
            const std::uint32_t access_error = 0x80110305U;
            std::memcpy(parameters + 28U, &access_error,
                        sizeof(access_error));
          } else {
            const auto read_guest_file = [&](std::size_t offset,
                                             const char* source_name) {
              std::uint32_t address{};
              std::uint32_t capacity{};
              std::memcpy(&address, parameters + offset, sizeof(address));
              std::memcpy(&capacity, parameters + offset + 4U,
                          sizeof(capacity));
              const auto bytes = read_binary_file(save_directory / source_name);
              const auto copied = static_cast<std::uint32_t>(
                  std::min<std::size_t>(bytes.size(), capacity));
              auto* output = psprecomp::mapped_address(state, address, copied);
              if (copied != 0U && output != nullptr)
                std::memcpy(output, bytes.data(), copied);
              std::memcpy(parameters + offset + 8U, &copied,
                          sizeof(copied));
            };
            read_guest_file(1412U, "ICON0.PNG");
            read_guest_file(1428U, "ICON1.PMF");
            read_guest_file(1444U, "PIC1.PNG");
            read_guest_file(1460U, "SND0.AT3");
            const auto sfo = read_binary_file(save_directory / "PARAM.SFO",
                                              1U << 20U);
            const auto copy_metadata = [&](std::size_t offset,
                                           std::size_t capacity,
                                           std::string_view key) {
              const auto value = utility::sfo_string(sfo, key);
              std::memset(parameters + offset, 0, capacity);
              std::memcpy(parameters + offset, value.data(),
                          std::min(value.size(), capacity - 1U));
            };
            copy_metadata(128U, 128U, "TITLE");
            copy_metadata(256U, 128U, "SAVEDATA_TITLE");
            copy_metadata(384U, 1024U, "SAVEDATA_DETAIL");
          }
        }
      } else if (save_mode) {
        std::uint32_t data_address{};
        std::uint32_t data_size{};
        std::memcpy(&data_address, parameters + 116U,
                    sizeof(data_address));
        std::memcpy(&data_size, parameters + 124U, sizeof(data_size));
        const auto* source =
            psprecomp::mapped_address(state, data_address, data_size);
        if (!valid_path || source == nullptr) {
          const std::uint32_t parameter_error = 0x80110388U;
          std::memcpy(parameters + 28U, &parameter_error,
                      sizeof(parameter_error));
        } else {
          const auto staging = save_directory.string() + ".psprism.tmp";
          const auto backup = save_directory.string() + ".psprism.backup";
          std::error_code directory_error;
          std::filesystem::remove_all(staging, directory_error);
          directory_error.clear();
          std::filesystem::create_directories(staging, directory_error);
          std::ofstream output(std::filesystem::path(staging) / file_name,
                               std::ios::binary | std::ios::trunc);
          output.write(reinterpret_cast<const char*>(source), data_size);
          const auto write_guest_file = [&](std::size_t offset,
                                            const char* destination) {
            std::uint32_t address{};
            std::uint32_t size{};
            std::memcpy(&address, parameters + offset, sizeof(address));
            std::memcpy(&size, parameters + offset + 8U, sizeof(size));
            const auto* bytes =
                psprecomp::mapped_address(state, address, size);
            if (size == 0U) return true;
            if (bytes == nullptr) return false;
            std::ofstream asset(std::filesystem::path(staging) / destination,
                                std::ios::binary | std::ios::trunc);
            asset.write(reinterpret_cast<const char*>(bytes), size);
            return static_cast<bool>(asset);
          };
          const bool assets_ok =
              write_guest_file(1412U, "ICON0.PNG") &&
              write_guest_file(1428U, "ICON1.PMF") &&
              write_guest_file(1444U, "PIC1.PNG") &&
              write_guest_file(1460U, "SND0.AT3");
          const auto sfo = utility::make_savedata_sfo(
              fixed_string(parameters + 128U, 128U),
              fixed_string(parameters + 256U, 128U),
              fixed_string(parameters + 384U, 1024U));
          std::ofstream sfo_output(
              std::filesystem::path(staging) / "PARAM.SFO",
              std::ios::binary | std::ios::trunc);
          sfo_output.write(reinterpret_cast<const char*>(sfo.data()),
                           sfo.size());
          output.close();
          sfo_output.close();
          if (directory_error || !output || !sfo_output || !assets_ok) {
            const std::uint32_t access_error = 0x80110385U;
            std::memcpy(parameters + 28U, &access_error,
                        sizeof(access_error));
            std::filesystem::remove_all(staging, directory_error);
          } else {
            std::filesystem::remove_all(backup, directory_error);
            directory_error.clear();
            const bool had_existing =
                std::filesystem::exists(save_directory, directory_error);
            if (had_existing)
              std::filesystem::rename(save_directory, backup,
                                      directory_error);
            if (!directory_error)
              std::filesystem::rename(staging, save_directory,
                                      directory_error);
            if (directory_error) {
              std::error_code rollback_error;
              std::filesystem::remove_all(save_directory, rollback_error);
              if (had_existing)
                std::filesystem::rename(backup, save_directory,
                                        rollback_error);
              const std::uint32_t access_error = 0x80110385U;
              std::memcpy(parameters + 28U, &access_error,
                          sizeof(access_error));
            } else {
              std::filesystem::remove_all(backup, directory_error);
            }
          }
        }
      } else if (delete_mode) {
        std::error_code delete_error;
        if (mode == 7U) {
          const auto save_root = implementation.configuration.writable_root /
                                 "PSP" / "SAVEDATA";
          for (const auto& selected : implementation.savedata_names)
            std::filesystem::remove_all(
                save_root / (std::string(game_name) + selected),
                delete_error);
        } else if (valid_directory) {
          std::filesystem::remove_all(save_directory, delete_error);
        }
        if (delete_error) {
          const std::uint32_t access_error = 0x80110385U;
          std::memcpy(parameters + 28U, &access_error,
                      sizeof(access_error));
        }
      }
      if (mode == 8U) {
        std::uint32_t free_info{};
        std::uint32_t data_info{};
        std::uint32_t utility_info{};
        std::memcpy(&free_info, parameters + 1488U, sizeof(free_info));
        std::memcpy(&data_info, parameters + 1492U, sizeof(data_info));
        std::memcpy(&utility_info, parameters + 1496U, sizeof(utility_info));
        std::error_code space_error;
        const auto space = std::filesystem::space(
            implementation.configuration.writable_root, space_error);
        const auto available =
            space_error ? 1024ULL * 1024ULL * 1024ULL : space.available;
        const auto free_kb = static_cast<std::uint32_t>(
            std::min<std::uintmax_t>(available / 1024U, 0x7fffffffU));
        if (auto* output = psprecomp::mapped_address(state, free_info, 20U)) {
          std::memset(output, 0, 20U);
          const std::uint32_t cluster_size = 32768U;
          const auto free_clusters = free_kb / 32U;
          std::memcpy(output, &cluster_size, sizeof(cluster_size));
          std::memcpy(output + 4U, &free_clusters, sizeof(free_clusters));
          std::memcpy(output + 8U, &free_kb, sizeof(free_kb));
          std::snprintf(reinterpret_cast<char*>(output + 12U), 8U, "%uMB",
                        free_kb / 1024U);
        }
        if (auto* output = psprecomp::mapped_address(state, data_info, 64U)) {
          std::memset(output + 36U, 0, 28U);
          char requested_game[14]{};
          char requested_save[21]{};
          std::memcpy(requested_game, output, 13U);
          std::memcpy(requested_save, output + 16U, 20U);
          const auto requested_directory =
              implementation.configuration.writable_root / "PSP" /
              "SAVEDATA" /
              (std::string(requested_game) + requested_save);
          std::error_code directory_error;
          if (!std::filesystem::is_directory(requested_directory,
                                             directory_error)) {
            const std::uint32_t no_data = 0x801103c7U;
            std::memcpy(parameters + 28U, &no_data, sizeof(no_data));
          }
        }
        if (auto* output =
                psprecomp::mapped_address(state, utility_info, 28U)) {
          std::uint32_t data_size{};
          std::memcpy(&data_size, parameters + 124U, sizeof(data_size));
          std::uint64_t requested_bytes = 2U * 32768U;
          const auto add_file = [&](std::uint32_t size) {
            if (size != 0U)
              requested_bytes += (static_cast<std::uint64_t>(size) +
                                  32767U) & ~32767ULL;
          };
          add_file(data_size + (data_size == 0U ? 0U : 16U));
          for (const auto offset : {1420U, 1436U, 1452U, 1468U}) {
            std::uint32_t size{};
            std::memcpy(&size, parameters + offset, sizeof(size));
            add_file(size);
          }
          const auto used_kb = static_cast<std::uint32_t>(
              std::min<std::uint64_t>(requested_bytes / 1024U,
                                      0x7fffffffU));
          const auto used_clusters = used_kb / 32U;
          std::memset(output, 0, 28U);
          std::memcpy(output, &used_clusters, sizeof(used_clusters));
          std::memcpy(output + 4U, &used_kb, sizeof(used_kb));
          std::snprintf(reinterpret_cast<char*>(output + 8U), 8U, "%u KB",
                        used_kb);
          std::memcpy(output + 16U, &used_kb, sizeof(used_kb));
          std::snprintf(reinterpret_cast<char*>(output + 20U), 8U, "%u KB",
                        used_kb);
        }
        if (implementation.verbose)
          std::fprintf(
              stderr, "[psprism:savedata] memory-stick free=%uKB\n",
              free_kb);
      }
      if (mode == 11U) {
        std::uint32_t list_address{};
        std::memcpy(&list_address, parameters + 1524U,
                    sizeof(list_address));
        if (auto* list = psprecomp::mapped_address(state, list_address, 12U)) {
          std::uint32_t maximum{};
          std::uint32_t entries_address{};
          std::memcpy(&maximum, list, sizeof(maximum));
          std::memcpy(&entries_address, list + 8U, sizeof(entries_address));
          maximum = std::min(maximum, 1024U);
          std::uint32_t count{};
          const auto root = implementation.configuration.writable_root /
                            "PSP" / "SAVEDATA";
          std::error_code list_error;
          for (const auto& entry :
               std::filesystem::directory_iterator(root, list_error)) {
            const auto full_name = entry.path().filename().string();
            if (!entry.is_directory() || !full_name.starts_with(game_name) ||
                count >= maximum)
              continue;
            auto* output = psprecomp::mapped_address(
                state, entries_address + count * 72U, 72U);
            if (output == nullptr) break;
            std::memset(output, 0, 72U);
            const std::uint32_t directory_mode = 0x11ffU;
            std::memcpy(output, &directory_mode, sizeof(directory_mode));
            const auto suffix = full_name.substr(std::strlen(game_name));
            std::memcpy(output + 52U, suffix.data(),
                        std::min<std::size_t>(suffix.size(), 19U));
            ++count;
          }
          std::memcpy(list + 4U, &count, sizeof(count));
        }
      }
      if (mode == 12U) {
        std::uint32_t list_address{};
        std::memcpy(&list_address, parameters + 1528U,
                    sizeof(list_address));
        if (auto* list = psprecomp::mapped_address(state, list_address, 36U)) {
          std::uint32_t max_normal{};
          std::uint32_t max_system{};
          std::uint32_t normal_address{};
          std::uint32_t system_address{};
          std::memcpy(&max_normal, list + 4U, sizeof(max_normal));
          std::memcpy(&max_system, list + 8U, sizeof(max_system));
          std::memcpy(&normal_address, list + 28U, sizeof(normal_address));
          std::memcpy(&system_address, list + 32U, sizeof(system_address));
          max_normal = std::min(max_normal, 1024U);
          max_system = std::min(max_system, 1024U);
          std::uint32_t normal_count{};
          std::uint32_t system_count{};
          std::error_code file_error;
          for (const auto& entry :
               std::filesystem::directory_iterator(save_directory,
                                                   file_error)) {
            if (!entry.is_regular_file()) continue;
            const auto filename = entry.path().filename().string();
            const bool system = is_one_of(
                filename, {"PARAM.SFO", "ICON0.PNG", "ICON1.PMF",
                           "PIC1.PNG", "SND0.AT3"});
            auto& count = system ? system_count : normal_count;
            const auto maximum = system ? max_system : max_normal;
            const auto address = system ? system_address : normal_address;
            if (count >= maximum) continue;
            auto* output = psprecomp::mapped_address(
                state, address + count * 80U, 80U);
            if (output == nullptr) continue;
            std::memset(output, 0, 80U);
            const std::uint32_t file_mode = 0x21ffU;
            const auto size = static_cast<std::uint64_t>(entry.file_size());
            std::memcpy(output, &file_mode, sizeof(file_mode));
            std::memcpy(output + 8U, &size, sizeof(size));
            std::memcpy(output + 64U, filename.data(),
                        std::min<std::size_t>(filename.size(), 15U));
            ++count;
          }
          std::memcpy(list + 16U, &normal_count, sizeof(normal_count));
          std::memcpy(list + 20U, &system_count, sizeof(system_count));
        }
      }
      if (mode == 22U) {
        std::uint32_t info_address{};
        std::memcpy(&info_address, parameters + 1532U,
                    sizeof(info_address));
        if (auto* info = psprecomp::mapped_address(state, info_address, 60U)) {
          std::uint64_t used_bytes{};
          std::error_code size_error;
          for (const auto& entry : std::filesystem::directory_iterator(
                   save_directory, size_error)) {
            if (entry.is_regular_file()) used_bytes += entry.file_size();
          }
          const auto used_kb = static_cast<std::uint32_t>(
              std::min<std::uint64_t>((used_bytes + 1023U) / 1024U,
                                      0x7fffffffU));
          const auto space = std::filesystem::space(
              implementation.configuration.writable_root, size_error);
          const auto free_kb = size_error ? 0U :
              static_cast<std::uint32_t>(std::min<std::uintmax_t>(
                  space.available / 1024U, 0x7fffffffU));
          std::memcpy(info + 24U, &free_kb, sizeof(free_kb));
          std::snprintf(reinterpret_cast<char*>(info + 28U), 8U, "%u KB",
                        free_kb);
          std::memcpy(info + 36U, &used_kb, sizeof(used_kb));
          std::snprintf(reinterpret_cast<char*>(info + 40U), 8U, "%u KB",
                        used_kb);
          std::memcpy(info + 48U, &used_kb, sizeof(used_kb));
          std::snprintf(reinterpret_cast<char*>(info + 52U), 8U, "%u KB",
                        used_kb);
        }
      }
    }
  }
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
