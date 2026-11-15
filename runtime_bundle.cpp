#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <objbase.h>
#include "runtime_bundle.h"
#include <cstring>
#include <stdexcept>
#include <string>

namespace {
std::filesystem::path runtimeDirectory, temporaryBase;
bool ownsRuntime = false;
struct Extraction { std::filesystem::path root; bool failed = false; unsigned count = 0; };
UINT CALLBACK ExtractFile(PVOID context, UINT message, UINT_PTR param1, UINT_PTR) {
    auto& state = *static_cast<Extraction*>(context);
    try {
        if (message == SPFILENOTIFY_FILEINCABINET) {
            auto* file = reinterpret_cast<FILE_IN_CABINET_INFO_W*>(param1);
            std::wstring member(file->NameInCabinet);
            for (auto& c : member) if (c == L'!') c = L'\\';
            std::filesystem::path relative(member);
            if (!IsSafeBundlePath(relative)) { state.failed = true; return FILEOP_ABORT; }
            auto target = state.root / relative;
            if (target.wstring().size() >= MAX_PATH) { state.failed = true; return FILEOP_ABORT; }
            std::filesystem::create_directories(target.parent_path());
            std::wcscpy(file->FullTargetName, target.c_str());
            ++state.count;
            return FILEOP_DOIT;
        }
        if (message == SPFILENOTIFY_FILEEXTRACTED) {
            const auto* file = reinterpret_cast<FILEPATHS_W*>(param1);
            if (file->Win32Error) { state.failed = true; return file->Win32Error; }
        }
        if (message == SPFILENOTIFY_NEEDNEWCABINET) { state.failed = true; return ERROR_INVALID_DATA; }
        return NO_ERROR;
    } catch (...) { state.failed = true; return ERROR_INVALID_DATA; }
}
}
bool IsSafeBundlePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& part : path) {
        const auto name = part.wstring();
        if (name.empty() || name == L"." || name == L".." || name.find_first_of(L":*?\"<>|") != std::wstring::npos) return false;
    }
    return true;
}
std::filesystem::path GetRuntimeDirectory() {
    if (!runtimeDirectory.empty()) return runtimeDirectory;
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, 32768);
    return std::filesystem::path(exe).parent_path();
}
void PrepareRuntime() {
    if (ownsRuntime) return;
    const auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(201), MAKEINTRESOURCEW(10));
    if (!resource) return; // Unbundled development executable.
    const auto loaded = LoadResource(nullptr, resource);
    const auto bytes = SizeofResource(nullptr, resource);
    const void* data = loaded ? LockResource(loaded) : nullptr;
    if (!data || !bytes) throw std::runtime_error("The embedded collector package is damaged. Download MonitorPC.exe again.");
    GUID id{}; wchar_t guid[40]{};
    if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, guid, 40)) throw std::runtime_error("Could not prepare the collector workspace.");
    temporaryBase = std::filesystem::temp_directory_path();
    // SetupAPI cabinet targets use MAX_PATH buffers. Prefer the existing short
    // form of the temp root, especially under long managed-user profiles.
    wchar_t shortBase[32768]{};
    const auto shortSize = GetShortPathNameW(temporaryBase.c_str(), shortBase, 32768);
    if (shortSize && shortSize < 32768) temporaryBase = shortBase;
    std::wstring suffix;
    for (const auto c : std::wstring(guid)) if (c != L'{' && c != L'}' && c != L'-') suffix += c;
    runtimeDirectory = temporaryBase / (std::wstring(L"MPC-") + suffix);
    if (!std::filesystem::create_directory(runtimeDirectory)) throw std::runtime_error("Could not create the collector workspace.");
    ownsRuntime = true;
    try {
        const auto cabinet = runtimeDirectory / L"collectors.cab";
        HANDLE output = CreateFileW(cabinet.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not unpack the embedded collectors.");
        DWORD written = 0;
        const bool saved = WriteFile(output, data, bytes, &written, nullptr) && written == bytes;
        CloseHandle(output);
        if (!saved) throw std::runtime_error("Could not write the embedded collector package.");
        Extraction state{runtimeDirectory};
        const bool extracted = SetupIterateCabinetW(cabinet.c_str(), 0, ExtractFile, &state);
        std::filesystem::remove(cabinet);
        if (!extracted || state.failed || !state.count ||
            !std::filesystem::is_regular_file(runtimeDirectory / L"sensors/MonitorSensors.exe") ||
            !std::filesystem::is_regular_file(runtimeDirectory / L"tools/PresentMon.exe"))
            throw std::runtime_error("The embedded collectors could not be prepared. Download MonitorPC.exe again.");
    } catch (...) { CleanupRuntime(); throw; }
}
void CleanupRuntime() {
    if (!ownsRuntime) return;
    // Never recursively remove a caller-provided path or a directory outside the
    // exact temporary parent used when this process created its private bundle.
    std::error_code error;
    const auto resolved = std::filesystem::weakly_canonical(runtimeDirectory, error);
    const auto parent = std::filesystem::weakly_canonical(temporaryBase, error);
    if (!error && resolved.parent_path() == parent &&
        resolved.filename().wstring().rfind(L"MPC-", 0) == 0) {
        // Windows may briefly retain file handles after collector termination.
        for (int attempt = 0; attempt < 30; ++attempt) {
            error.clear();
            std::filesystem::remove_all(resolved, error);
            if (!error && !std::filesystem::exists(resolved)) break;
            Sleep(200);
        }
    }
    runtimeDirectory.clear(); ownsRuntime = false;
}
