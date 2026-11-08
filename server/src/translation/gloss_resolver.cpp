#include "translation/gloss_resolver.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace translation
{
namespace
{
constexpr auto kNegativeTtl = std::chrono::seconds(30);
constexpr auto kUnavailableCooldown = std::chrono::seconds(15);
constexpr auto kInferBudget = std::chrono::seconds(8); // 第一次要先载入模型
constexpr size_t kMaxEligibleChars = 6;
constexpr size_t kMaxEnglishBytes = 80;

std::string_view Trim(std::string_view value)
{
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

// 解一个 UTF-8 码点；格式不对返回 0 并前进一个字节。
char32_t DecodeUtf8(std::string_view text, size_t &index)
{
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    size_t length = 1;
    char32_t code = lead;
    if (lead >= 0xF0 && lead <= 0xF7)
    {
        length = 4;
        code = lead & 0x07;
    }
    else if (lead >= 0xE0)
    {
        length = 3;
        code = lead & 0x0F;
    }
    else if (lead >= 0xC0)
    {
        length = 2;
        code = lead & 0x1F;
    }
    else if (lead >= 0x80)
    {
        ++index;
        return 0;
    }
    if (length == 1 || index + length > text.size())
    {
        ++index;
        return length == 1 ? code : 0;
    }
    for (size_t i = 1; i < length; ++i)
    {
        const unsigned char next = static_cast<unsigned char>(text[index + i]);
        if ((next & 0xC0) != 0x80)
        {
            ++index;
            return 0;
        }
        code = (code << 6) | (next & 0x3F);
    }
    index += length;
    return code;
}

bool IsHan(char32_t code)
{
    return (code >= 0x3400 && code <= 0x4DBF) || (code >= 0x4E00 && code <= 0x9FFF) ||
           (code >= 0xF900 && code <= 0xFAFF) || (code >= 0x20000 && code <= 0x2FA1F);
}

bool ContainsHan(std::string_view text)
{
    for (size_t i = 0; i < text.size();)
        if (IsHan(DecodeUtf8(text, i)))
            return true;
    return false;
}
} // namespace

void GlossOverlay::Parse(std::string_view tsv)
{
    entries_.clear();
    bool header = true;
    size_t start = 0;
    while (start <= tsv.size())
    {
        const size_t end = tsv.find('\n', start);
        std::string_view line = end == std::string_view::npos ? tsv.substr(start) : tsv.substr(start, end - start);
        start = end == std::string_view::npos ? tsv.size() + 1 : end + 1;
        if (header)
        {
            header = false;
            if (line.size() >= 3 && line.substr(0, 3) == "\xEF\xBB\xBF")
                line.remove_prefix(3);
            if (Trim(line).substr(0, 7) == "chinese")
                continue;
        }
        line = Trim(line);
        if (line.empty() || line.front() == '#')
            continue;
        const size_t tab = line.find('\t');
        if (tab == std::string_view::npos)
            continue;
        const std::string_view chinese = Trim(line.substr(0, tab));
        const std::string_view gloss = Trim(line.substr(tab + 1));
        if (chinese.empty() || gloss.empty())
            continue;
        entries_.emplace(std::string(chinese), std::string(gloss));
    }
}

bool GlossOverlay::Load(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        entries_.clear();
        return false;
    }
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Parse(content);
    return true;
}

GlossOverlay::Entry GlossOverlay::Find(const std::string &chinese) const
{
    const auto found = entries_.find(chinese);
    if (found == entries_.end())
        return {};
    if (found->second == "-")
        return {Kind::Hidden, {}};
    return {Kind::Text, found->second};
}

ModelGlossCache::ModelGlossCache(Worker::Infer infer, Notify notify, Now now)
    : infer_(std::move(infer)), notify_(std::move(notify)), now_(std::move(now)), thread_(&ModelGlossCache::Run, this)
{
}

ModelGlossCache::~ModelGlossCache()
{
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        pending_.clear();
    }
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

Time ModelGlossCache::Tick() const
{
    return now_ ? now_() : Clock::now();
}

bool ModelGlossCache::Eligible(const std::string &chinese)
{
    if (chinese.empty())
        return false;
    size_t count = 0;
    for (size_t i = 0; i < chinese.size();)
    {
        if (!IsHan(DecodeUtf8(chinese, i)))
            return false;
        if (++count > kMaxEligibleChars)
            return false;
    }
    return true;
}

std::string ModelGlossCache::Clean(std::string english)
{
    std::string_view text = Trim(english);
    const size_t newline = text.find('\n');
    if (newline != std::string_view::npos)
        text = Trim(text.substr(0, newline));
    while (text.size() >= 2 &&
           ((text.front() == '"' && text.back() == '"') || (text.front() == '\'' && text.back() == '\'')))
        text = Trim(text.substr(1, text.size() - 2));
    if (!text.empty() && text.back() == '.' && text.find(' ') != std::string_view::npos)
        text.remove_suffix(1);
    else if (text.size() > 1 && text.back() == '.' && text.find('.') == text.size() - 1)
        text.remove_suffix(1);
    text = Trim(text);
    if (text.empty() || text.size() > kMaxEnglishBytes || ContainsHan(text))
        return {};
    return std::string(text);
}

std::string ModelGlossCache::Lookup(const std::string &chinese)
{
    if (!Eligible(chinese))
        return {};
    bool queued = false;
    {
        std::lock_guard lock(mutex_);
        if (stop_)
            return {};
        const Time now = Tick();
        const auto found = cache_.find(chinese);
        if (found != cache_.end())
        {
            if (!found->second.text.empty() || now - found->second.at < kNegativeTtl)
                return found->second.text;
            cache_.erase(found);
        }
        if (now < unavailable_until_ || running_ == chinese ||
            std::find(pending_.begin(), pending_.end(), chinese) != pending_.end())
            return {};
        if (pending_.size() >= kMaxPending)
            pending_.erase(pending_.begin());
        pending_.push_back(chinese);
        queued = true;
    }
    if (queued)
        wake_.notify_one();
    return {};
}

void ModelGlossCache::Run()
{
    for (;;)
    {
        std::string key;
        uint64_t generation = 0;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stop_ || !pending_.empty(); });
            if (stop_)
                return;
            key = std::move(pending_.back());
            pending_.pop_back();
            running_ = key;
            generation = ++generation_;
        }

        Result result{generation, Status::Error, {}};
        try
        {
            const Request request{generation, key, Tick() + kInferBudget};
            result = infer_(request, [this] {
                std::lock_guard lock(mutex_);
                return stop_;
            });
        }
        catch (...)
        {
            // 失败只当没有结果；不记录原文。
        }

        bool changed = false;
        {
            std::lock_guard lock(mutex_);
            running_.clear();
            if (stop_)
                return;
            const Time now = Tick();
            if (result.status == Status::Missing)
            {
                // 模型没装：不缓存，也别每个按键都来问一次，等一会儿再试（装好后自然恢复）。
                unavailable_until_ = now + kUnavailableCooldown;
                pending_.clear();
            }
            else
            {
                std::string english = result.status == Status::Ready ? Clean(std::move(result.english)) : std::string{};
                changed = !english.empty();
                if (cache_.size() >= kMaxCached && cache_.find(key) == cache_.end())
                    cache_.clear();
                cache_[key] = {std::move(english), now};
            }
        }
        if (changed && notify_)
        {
            try
            {
                notify_();
            }
            catch (...)
            {
            }
        }
    }
}

GlossResolver::Answer GlossResolver::Resolve(const std::string &chinese, const Dictionary &user,
                                             const Dictionary &dictionary) const
{
    if (user)
    {
        std::string custom = user();
        if (!custom.empty())
            return {std::move(custom), false};
    }
    const GlossOverlay::Entry entry = overlay_.Find(chinese);
    if (entry.kind == GlossOverlay::Kind::Hidden)
        return {{}, true};
    if (entry.kind == GlossOverlay::Kind::Text)
        return {entry.text, false};
    if (dictionary)
    {
        std::string gloss = dictionary();
        if (!gloss.empty())
            return {std::move(gloss), false};
    }
    if (model_)
        return {model_->Lookup(chinese), false};
    return {};
}
} // namespace translation
