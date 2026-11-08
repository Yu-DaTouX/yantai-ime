#include "translation/local_engine.h"

#include <Windows.h>
#include <array>

namespace translation
{
LocalEngine::~LocalEngine()
{
    Unload();
}

void LocalEngine::Unload()
{
    if (engine_ && api_.destroy)
        api_.destroy(engine_);
    engine_ = nullptr;
    api_ = {};
    if (module_)
        FreeLibrary(static_cast<HMODULE>(module_));
    module_ = nullptr;
}

bool LocalEngine::FilesPresent(const std::filesystem::path &directory)
{
    std::error_code error;
    if (std::filesystem::exists(directory / ".pending-delete", error) || error)
        return false;
    for (const char *file :
         {"yantai_translate.dll", "model.bin", "config.json", "shared_vocabulary.json", "source.spm", "target.spm"})
    {
        if (!std::filesystem::is_regular_file(directory / file, error) || error)
            return false;
    }
    return true;
}

bool LocalEngine::Load(const std::filesystem::path &directory)
{
    Unload();
    if (!FilesPresent(directory))
        return false;
    std::error_code error;
    const auto absolute = std::filesystem::absolute(directory, error);
    if (error)
        return false;
    // Search only this component directory and Windows system libraries, never cwd/PATH.
    const auto module = LoadLibraryExW((absolute / "yantai_translate.dll").c_str(), nullptr,
                                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module)
        return false;
    module_ = module;
    const auto get_api = reinterpret_cast<YtTranslateGetApi>(GetProcAddress(module, "yt_translate_get_api"));
    api_.struct_size = sizeof(api_);
    if (!get_api || get_api(1, &api_) != YT_TRANSLATE_OK || api_.version != 1 || api_.struct_size != sizeof(api_) ||
        !api_.create || !api_.destroy || !api_.translate)
    {
        Unload();
        return false;
    }
    // oneDNN/MSVC OpenMP keeps TLS on its runtime threads after a model is destroyed.
    // FreeLibrary after inference caused a reproducible access violation in the isolated
    // smoke executable. Pin ONLY the validated engine module until process exit; Unload
    // still releases the entire model. Settings deletion must use deferred-delete on DLLs.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(get_api), &pinned) ||
        api_.create(absolute.u8string().c_str(), &engine_) != YT_TRANSLATE_OK || !engine_)
    {
        Unload();
        return false;
    }
    return true;
}

int32_t LocalEngine::Translate(const std::string &source, uint32_t timeout_ms, YtTranslateCancelled cancelled,
                               void *context, std::string &english)
{
    english.clear();
    if (!loaded())
        return YT_TRANSLATE_UNAVAILABLE;
    if (source.size() > 65536)
        return YT_TRANSLATE_INVALID_ARGUMENT;
    std::array<char, 32768> output{};
    uint32_t size = 0;
    const int32_t status =
        api_.translate(engine_, source.data(), static_cast<uint32_t>(source.size()), timeout_ms, cancelled, context,
                       output.data(), static_cast<uint32_t>(output.size()), &size);
    if (status == YT_TRANSLATE_OK)
    {
        if (size >= output.size() || output[size] != '\0')
            return YT_TRANSLATE_ERROR;
        english.assign(output.data(), size);
    }
    return status;
}
} // namespace translation
