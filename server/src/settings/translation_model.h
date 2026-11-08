#pragma once

#include "settings/model_download.h"
#include "translation/engine_api.h"

namespace translation_model
{
inline constexpr char kDirectory[] = "translate-zh-en";
inline constexpr char kReceipt[] = YT_TRANSLATE_COMPONENT_RECEIPT;
inline std::filesystem::path Directory()
{
    return metasequoia::data_directory() / "models" / kDirectory;
}

// A receipt is published only after SHA256-checked archive extraction has finished.
// Do not use the single-file resolver: deleting a receipt does not mean a pinned DLL is gone.
inline std::filesystem::path ResolveDirectory()
{
    const auto directory = Directory();
    const auto marker = directory / model_download::kPendingDeleteMarker;
    std::error_code error;
    if (std::filesystem::exists(marker, error))
    {
        bool removed_all = true;
        std::filesystem::directory_iterator entry(directory, error), end;
        for (; !error && entry != end; entry.increment(error))
        {
            if (entry->path() == marker)
                continue;
            std::error_code removal_error;
            std::filesystem::remove_all(entry->path(), removal_error);
            if (removal_error)
                removed_all = false;
        }
        if (removed_all && !error)
        {
            std::filesystem::remove(marker, error);
            std::filesystem::remove(directory, error);
        }
        return {};
    }
    if (!error && std::filesystem::is_regular_file(directory / kReceipt, error) && !error)
        return directory;
    return {};
}

std::string SourceUrl();
model_download::ModelStatus GetModelStatus();
bool VerifyModelFile(const std::filesystem::path &file, std::string &error);
bool StartDownload();
bool ImportModel(const std::filesystem::path &source);
std::filesystem::path PromptForModelFile(HWND owner);
bool DeleteModel();
} // namespace translation_model
