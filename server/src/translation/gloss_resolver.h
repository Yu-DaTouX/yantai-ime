#pragma once

// 候选词英文行的释义来源，按优先级：
//   0. 用户的 custom_translations.txt（永远最优先）；
//   1. 随包覆盖表 assets\dictionary\zh_en_gloss.tsv（chinese<TAB>gloss；gloss 为 "-" 表示该词不显示英文行）；
//   2. english.db 的中译英表（含 custom_translations.txt 覆盖）；
//   3. 本地翻译模型：后台、按词缓存、不阻塞按键；模型没装就什么都不显示。
// 纯逻辑，不依赖 Win32 / 配置 / 管道，测试可以直接注入推理函数。

#include "translation/translation_worker.h"

#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace translation
{
class GlossOverlay
{
  public:
    enum class Kind
    {
        Absent,
        Hidden,
        Text
    };
    struct Entry
    {
        Kind kind = Kind::Absent;
        std::string text;
    };

    // 第一行是表头 "chinese<TAB>gloss"；空行、以 '#' 开头的行与缺列的行忽略；同一个词以先出现的为准。
    void Parse(std::string_view tsv);
    bool Load(const std::filesystem::path &path);
    Entry Find(const std::string &chinese) const;
    size_t size() const
    {
        return entries_.size();
    }

  private:
    std::unordered_map<std::string, std::string> entries_; // "-" 保存为隐藏
};

// 本地模型兜底：Lookup 永远不等推理。第一次遇到的词排进后台队列，算完写进缓存并通知调用方重查；
// 之后同一个词直接命中缓存。模型没装（Missing）不缓存、也不重复叩门（冷却一小段时间）。
class ModelGlossCache
{
  public:
    using Notify = std::function<void()>;
    using Now = std::function<Time()>;
    ModelGlossCache(Worker::Infer infer, Notify notify, Now now = {});
    ~ModelGlossCache();
    ModelGlossCache(const ModelGlossCache &) = delete;
    ModelGlossCache &operator=(const ModelGlossCache &) = delete;

    // 已缓存的英文；没有（还在排队、模型不可用、失败）返回空串。
    std::string Lookup(const std::string &chinese);

    // 只给短的纯汉字词（最多 6 个字）查模型：整句和夹杂字母数字的候选不是「单词释义」。
    static bool Eligible(const std::string &chinese);
    // 去掉模型输出的首尾空白、引号、句末句号，只取第一行；太长或仍含汉字的视为没有结果。
    static std::string Clean(std::string english);

    static constexpr size_t kMaxCached = 2048;
    static constexpr size_t kMaxPending = 16;

  private:
    struct Cached
    {
        std::string text; // 空串 = 这次没有结果
        Time at{};
    };
    void Run();
    Time Tick() const;

    Worker::Infer infer_;
    Notify notify_;
    Now now_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::unordered_map<std::string, Cached> cache_;
    std::vector<std::string> pending_; // 最新的排在后面，先算最新的
    std::string running_;
    Time unavailable_until_{};
    uint64_t generation_ = 0;
    bool stop_ = false;
    std::thread thread_;
};

class GlossResolver
{
  public:
    struct Answer
    {
        std::string text;
        bool hidden = false; // 覆盖表标了 "-"：这个词不显示英文行
    };
    using Dictionary = std::function<std::string()>;

    // model 可以为空（不兜底）。resolver 不拥有它。
    GlossResolver(GlossOverlay overlay, ModelGlossCache *model) : overlay_(std::move(overlay)), model_(model)
    {
    }
    Answer Resolve(const std::string &chinese, const Dictionary &user, const Dictionary &dictionary) const;

  private:
    GlossOverlay overlay_;
    ModelGlossCache *model_;
};
} // namespace translation
