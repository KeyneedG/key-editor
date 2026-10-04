#include "tiny3d/editor.hpp"
#if defined(_WIN32) && !defined(TINY3D_HEADLESS)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <stdexcept>

namespace tiny3d::editor {
std::filesystem::path selectProjectFolder(const std::filesystem::path& projects) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) throw std::runtime_error("Cannot initialize the project folder dialog");
    struct Session { ~Session() { CoUninitialize(); } } session;
    IFileOpenDialog* picker = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&picker))))
        throw std::runtime_error("Cannot create the project folder dialog");
    struct Release { IFileOpenDialog* value; ~Release() { value->Release(); } } release{picker};
    DWORD options = 0; picker->GetOptions(&options);
    picker->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    picker->SetTitle(L"Open EryEngine Project");
    IShellItem* initial = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(projects.c_str(), nullptr, IID_PPV_ARGS(&initial)))) {
        picker->SetFolder(initial); initial->Release();
    }
    const HRESULT result = picker->Show(GetActiveWindow());
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
    if (FAILED(result)) throw std::runtime_error("Project folder selection failed");
    IShellItem* item = nullptr;
    if (FAILED(picker->GetResult(&item))) throw std::runtime_error("Cannot read the selected project folder");
    PWSTR path = nullptr; const auto hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path); item->Release();
    if (FAILED(hr)) throw std::runtime_error("Cannot read the selected project path");
    std::filesystem::path directory(path); CoTaskMemFree(path); return directory;
}
}
#else
namespace tiny3d::editor {
std::filesystem::path selectProjectFolder(const std::filesystem::path&) { return {}; }
}
#endif
