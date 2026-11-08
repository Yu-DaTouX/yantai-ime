#pragma once

#include "translation/engine_api.h"

#include <filesystem>
#include <string>

namespace translation
{
// Single-background-thread owner. Does not link to CT2, SentencePiece or a Python runtime.
class LocalEngine
{
  public:
    LocalEngine() = default;
    ~LocalEngine();
    LocalEngine(const LocalEngine &) = delete;
    LocalEngine &operator=(const LocalEngine &) = delete;

    bool Load(const std::filesystem::path &directory);
    void Unload();
    bool loaded() const
    {
        return engine_ != nullptr;
    }
    int32_t Translate(const std::string &source, uint32_t timeout_ms, YtTranslateCancelled cancelled, void *context,
                      std::string &english);
    static bool FilesPresent(const std::filesystem::path &directory);

  private:
    void *module_ = nullptr;
    void *engine_ = nullptr;
    YtTranslateApi api_{};
};
} // namespace translation
