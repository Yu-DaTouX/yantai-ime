#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace FanyImeTranslationCommit
{
// Fragments retain the published fixed-size pipe frame. UTF-16 is concatenated before any
// edit session, so even a surrogate split across frames never reaches the document.
class Assembly
{
  public:
    bool Append(uint64_t request, const std::wstring &part)
    {
        if (!request || (text_.count(request) == 0 && text_.size() >= 8))
            return false;
        auto &text = text_[request];
        if (text.size() + part.size() > 131072)
        {
            text_.erase(request);
            return false;
        }
        text += part;
        return true;
    }
    std::wstring Take(uint64_t request)
    {
        auto found = text_.find(request);
        if (found == text_.end())
            return {};
        auto text = std::move(found->second);
        text_.erase(found);
        return text;
    }
    void Clear()
    {
        text_.clear();
    }

  private:
    std::map<uint64_t, std::wstring> text_;
};
} // namespace FanyImeTranslationCommit
