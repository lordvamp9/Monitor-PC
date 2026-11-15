#pragma once
#include <filesystem>

// Embedded collectors are extracted privately for the lifetime of this process.
// Development builds without a bundle use the executable's directory.
void PrepareRuntime();
void CleanupRuntime();
std::filesystem::path GetRuntimeDirectory();
bool IsSafeBundlePath(const std::filesystem::path& path);
