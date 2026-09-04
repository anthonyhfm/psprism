    // The host adapter and native entry point deliberately share one ABI
    // across desktop targets. Platform-specific behavior lives in Refract;
    // adding another native backend therefore does not duplicate generated
    // import dispatch or process setup.
    std::filesystem::copy_file(
        platform_directory / "macos" / "platform.cpp",
        platform_directory / "windows" / "platform.cpp",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(
        platform_directory / "macos" / "main.cpp",
        platform_directory / "windows" / "main.cpp",
        std::filesystem::copy_options::overwrite_existing);
