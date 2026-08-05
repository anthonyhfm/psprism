void sceUtilitySavedataInitStart(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  constexpr std::size_t minimum_parameter_size = 128U;
  auto* parameters =
      psprecomp::mapped_address(state, state.gpr[4], minimum_parameter_size);
  if (parameters == nullptr) {
    state.gpr[2] = 0x80110388U;
    return;
  }
  std::uint32_t expected{};
  if (!implementation.active_utility.compare_exchange_strong(expected, 1U)) {
    state.gpr[2] = utility_busy;
    return;
  }
  std::uint32_t mode{};
  std::memcpy(&mode, parameters + 48U, sizeof(mode));
  char game_name[14]{};
  char save_name[21]{};
  char file_name[14]{};
  std::memcpy(game_name, parameters + 60U, 13U);
  std::memcpy(save_name, parameters + 76U, 20U);
  std::memcpy(file_name, parameters + 100U, 13U);
  implementation.savedata_parameters = state.gpr[4];
  implementation.savedata_operation_complete = false;
  implementation.savedata_dialog_presented = false;
  implementation.savedata_names.clear();
  implementation.savedata_dialog_id = implementation.next_dialog_id++;
  implementation.savedata_status = 1U;
  
  std::uint32_t name_list_address{};
  std::memcpy(&name_list_address, parameters + 96U,
              sizeof(name_list_address));
  if (name_list_address != 0U) {
    for (std::size_t index = 0; index < 128U; ++index) {
      const auto* entry = psprecomp::mapped_address(
          state, name_list_address + static_cast<std::uint32_t>(index * 20U),
          20U);
      if (entry == nullptr) break;
      const auto value = fixed_string(entry, 20U);
      if (value.empty()) break;
      implementation.savedata_names.push_back(value);
    }
  }
  if (implementation.savedata_names.empty() && save_name[0] != '\0')
    implementation.savedata_names.emplace_back(save_name);
  
  if (mode == 7U && implementation.savedata_names.empty()) {
    const auto root = implementation.configuration.writable_root / "PSP" /
                      "SAVEDATA";
    std::error_code directory_error;
    for (const auto& entry :
         std::filesystem::directory_iterator(root, directory_error)) {
      const auto value = entry.path().filename().string();
      if (entry.is_directory() && value.starts_with(game_name))
        implementation.savedata_names.push_back(
            value.substr(std::strlen(game_name)));
    }
  }
  
  const bool interactive = mode >= 2U && mode <= 7U;
  if (interactive) {
    std::uint32_t language{};
    std::uint32_t button_swap{};
    std::memcpy(&language, parameters + 4U, sizeof(language));
    std::memcpy(&button_swap, parameters + 8U, sizeof(button_swap));
    const auto text = utility_text(language);
    host::DialogModel dialog;
    dialog.id = implementation.savedata_dialog_id;
    dialog.kind = mode == 2U || mode == 4U
                      ? host::DialogKind::savedata_load
                  : mode == 6U || mode == 7U
                      ? host::DialogKind::savedata_delete
                      : host::DialogKind::savedata_save;
    dialog.title = dialog.kind == host::DialogKind::savedata_load
                       ? text.load
                   : dialog.kind == host::DialogKind::savedata_delete
                       ? text.remove
                       : text.save;
    dialog.accept_label = dialog.kind == host::DialogKind::savedata_load
                              ? text.load
                          : dialog.kind == host::DialogKind::savedata_delete
                              ? text.remove
                              : text.save;
    dialog.cancel_label = text.back;
    dialog.confirm_with_cross = button_swap != 0U;
    const auto save_root = implementation.configuration.writable_root /
                           "PSP" / "SAVEDATA";
    for (const auto& entry_name : implementation.savedata_names) {
      host::DialogItem item;
      const auto directory =
          save_root / (std::string(game_name) + entry_name);
      std::error_code item_error;
      item.empty = !std::filesystem::is_directory(directory, item_error);
      if (item.empty) {
        item.title = text.empty;
        item.detail = entry_name;
      } else {
        const auto sfo = read_binary_file(directory / "PARAM.SFO", 1U << 20U);
        item.title = utility::sfo_string(sfo, "TITLE");
        item.subtitle = utility::sfo_string(sfo, "SAVEDATA_TITLE");
        if (item.title.empty()) item.title = item.subtitle;
        if (item.title.empty()) item.title = entry_name;
        item.detail = utility::sfo_string(sfo, "SAVEDATA_DETAIL");
        item.timestamp = file_timestamp(directory);
        item.size = directory_size_label(directory);
        item.icon_png = read_binary_file(directory / "ICON0.PNG", 4U << 20U);
        item.preview_png = read_binary_file(directory / "PIC1.PNG", 4U << 20U);
      }
      dialog.items.push_back(std::move(item));
    }
    if (dialog.items.empty() &&
        dialog.kind == host::DialogKind::savedata_save) {
      implementation.savedata_names.emplace_back(save_name);
      dialog.items.push_back(
          {text.empty, {}, save_name, {}, {}, {}, {}, true});
    }
    std::uint32_t focus{};
    if (auto* complete = psprecomp::mapped_address(
            state, state.gpr[4], 1484U))
      std::memcpy(&focus, complete + 1480U, sizeof(focus));
    if (!dialog.items.empty()) {
      if (focus == 2U || focus == 6U || focus == 8U)
        dialog.selected_item = dialog.items.size() - 1U;
      if (focus == 3U || focus == 4U) {
        std::error_code newest_error;
        std::filesystem::file_time_type chosen{};
        for (std::size_t index = 0; index < dialog.items.size(); ++index) {
          const auto directory = save_root /
              (std::string(game_name) + implementation.savedata_names[index]);
          const auto time = std::filesystem::last_write_time(directory,
                                                             newest_error);
          if (!newest_error && (index == 0U ||
              (focus == 3U ? time > chosen : time < chosen))) {
            chosen = time;
            dialog.selected_item = index;
          }
          newest_error.clear();
        }
      }
      if (focus == 7U || focus == 8U) {
        for (std::size_t index = 0; index < dialog.items.size(); ++index) {
          const auto candidate = focus == 7U ? index
              : dialog.items.size() - 1U - index;
          if (dialog.items[candidate].empty) {
            dialog.selected_item = candidate;
            break;
          }
        }
      }
    }
    host::present_dialog(std::move(dialog));
    implementation.savedata_dialog_presented = true;
  }
  if (implementation.verbose)
    std::fprintf(
        stderr,
        "[psprism:savedata] init mode=%u game=%s save=%s file=%s\n", mode,
        game_name, save_name, file_name);
  state.gpr[2] = 0U;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
