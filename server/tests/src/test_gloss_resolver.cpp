#include "tests/includes/test_framework.h"
#include "translation/gloss_resolver.h"

#include <atomic>
#include <map>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>

using namespace translation;
using namespace std::chrono_literals;

namespace
{
// 记录推理调用的假模型。回答表里没有的词按 Missing 处理。
struct FakeModel
{
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<std::string> calls;
    std::map<std::string, std::pair<Status, std::string>> answers;
    bool block = false;

    Worker::Infer Infer()
    {
        return [this](const Request &request, const Worker::Cancelled &) {
            std::unique_lock lock(mutex);
            wake.wait_for(lock, 3s, [&] { return !block; });
            calls.push_back(request.chinese);
            wake.notify_all();
            const auto answer = answers.find(request.chinese);
            if (answer == answers.end())
                return Result{request.generation, Status::Missing, {}};
            return Result{request.generation, answer->second.first, answer->second.second};
        };
    }
    size_t CallCount()
    {
        std::lock_guard lock(mutex);
        return calls.size();
    }
    bool WaitForCalls(size_t count)
    {
        std::unique_lock lock(mutex);
        return wake.wait_for(lock, 3s, [&] { return calls.size() >= count; });
    }
};

struct Notifications
{
    std::mutex mutex;
    std::condition_variable wake;
    int count = 0;
    void Notify()
    {
        std::lock_guard lock(mutex);
        ++count;
        wake.notify_all();
    }
    bool WaitFor(int expected)
    {
        std::unique_lock lock(mutex);
        return wake.wait_for(lock, 3s, [&] { return count >= expected; });
    }
    int Count()
    {
        std::lock_guard lock(mutex);
        return count;
    }
};

GlossOverlay MakeOverlay(const char *tsv)
{
    GlossOverlay overlay;
    overlay.Parse(tsv);
    return overlay;
}

GlossResolver::Dictionary Dictionary(std::string value, int *calls = nullptr)
{
    return [value = std::move(value), calls] {
        if (calls)
            ++*calls;
        return value;
    };
}
} // namespace

TEST_CASE(gloss_overlay_parses_text_hidden_and_ignores_noise)
{
    const GlossOverlay overlay = MakeOverlay("\xEF\xBB\xBF"
                                             "chinese\tgloss\r\n"
                                             "\xE6\x88\x91\xE4\xBB\xAC\twe; us\r\n" // 我们
                                             "\xE7\x9A\x84\t-\r\n"                  // 的
                                             "# comment\tignored\r\n"
                                             "\r\n"
                                             "no-tab-line\r\n"
                                             "\xE5\xA4\xB4\t\r\n"                  // 头 without gloss
                                             "\xE5\xA4\xA9\xE6\xB0\x94\tweather\n" // 天气
                                             "\xE5\xA4\xA9\xE6\xB0\x94\tsky\n");   // duplicate: first wins
    REQUIRE_EQ(overlay.size(), static_cast<size_t>(3));
    const auto we = overlay.Find("\xE6\x88\x91\xE4\xBB\xAC");
    REQUIRE(we.kind == GlossOverlay::Kind::Text);
    REQUIRE_EQ(we.text, std::string("we; us"));
    REQUIRE(overlay.Find("\xE7\x9A\x84").kind == GlossOverlay::Kind::Hidden);
    REQUIRE(overlay.Find("\xE5\xA4\xB4").kind == GlossOverlay::Kind::Absent);
    REQUIRE_EQ(overlay.Find("\xE5\xA4\xA9\xE6\xB0\x94").text, std::string("weather"));
    REQUIRE(overlay.Find("missing").kind == GlossOverlay::Kind::Absent);
}

TEST_CASE(gloss_overlay_wins_over_dictionary_and_model)
{
    FakeModel model;
    model.answers["\xE5\xA4\xA9\xE6\xB0\x94"] = {Status::Ready, "model weather"};
    Notifications notifications;
    ModelGlossCache cache(model.Infer(), [&] { notifications.Notify(); });
    const GlossResolver resolver(MakeOverlay("chinese\tgloss\n\xE5\xA4\xA9\xE6\xB0\x94\tweather\n"), &cache);

    int dictionary_calls = 0;
    const auto answer =
        resolver.Resolve("\xE5\xA4\xA9\xE6\xB0\x94", Dictionary("dictionary weather", &dictionary_calls));
    REQUIRE_EQ(answer.text, std::string("weather"));
    REQUIRE(!answer.hidden);
    REQUIRE_EQ(dictionary_calls, 0);
    REQUIRE_EQ(model.CallCount(), static_cast<size_t>(0));
}

TEST_CASE(gloss_overlay_dash_hides_the_word_and_never_asks_dictionary_or_model)
{
    FakeModel model;
    model.answers["\xE7\x9A\x84"] = {Status::Ready, "of"};
    ModelGlossCache cache(model.Infer(), [] {});
    const GlossResolver resolver(MakeOverlay("chinese\tgloss\n\xE7\x9A\x84\t-\n"), &cache);

    int dictionary_calls = 0;
    const auto answer = resolver.Resolve("\xE7\x9A\x84", Dictionary("of; 's", &dictionary_calls));
    REQUIRE(answer.hidden);
    REQUIRE(answer.text.empty());
    REQUIRE_EQ(dictionary_calls, 0);
    std::this_thread::sleep_for(50ms);
    REQUIRE_EQ(model.CallCount(), static_cast<size_t>(0));
}

TEST_CASE(gloss_dictionary_wins_over_model_when_overlay_has_no_entry)
{
    FakeModel model;
    model.answers["\xE8\x8C\xB6"] = {Status::Ready, "model tea"};
    ModelGlossCache cache(model.Infer(), [] {});
    const GlossResolver resolver(MakeOverlay("chinese\tgloss\n"), &cache);

    const auto answer = resolver.Resolve("\xE8\x8C\xB6", Dictionary("tea"));
    REQUIRE_EQ(answer.text, std::string("tea"));
    REQUIRE(!answer.hidden);
    std::this_thread::sleep_for(50ms);
    REQUIRE_EQ(model.CallCount(), static_cast<size_t>(0));
}

TEST_CASE(gloss_model_fallback_is_asynchronous_cached_and_notifies)
{
    FakeModel model;
    model.block = true; // 推理在跑时 Resolve 也必须立刻返回
    model.answers["\xE5\xBF\xAB\xE9\x80\x92"] = {Status::Ready, "\"Express delivery.\"\n"};
    Notifications notifications;
    ModelGlossCache cache(model.Infer(), [&] { notifications.Notify(); });
    const GlossResolver resolver(MakeOverlay("chinese\tgloss\n"), &cache);

    const auto first = resolver.Resolve("\xE5\xBF\xAB\xE9\x80\x92", Dictionary(""));
    REQUIRE(first.text.empty());
    REQUIRE(!first.hidden);
    // 排队中再问不会重复排队。
    REQUIRE(resolver.Resolve("\xE5\xBF\xAB\xE9\x80\x92", Dictionary("")).text.empty());

    {
        std::lock_guard lock(model.mutex);
        model.block = false;
    }
    model.wake.notify_all();
    REQUIRE(notifications.WaitFor(1));

    const auto second = resolver.Resolve("\xE5\xBF\xAB\xE9\x80\x92", Dictionary(""));
    REQUIRE_EQ(second.text, std::string("Express delivery"));
    REQUIRE_EQ(model.CallCount(), static_cast<size_t>(1)); // 命中缓存，不再推理
    REQUIRE_EQ(resolver.Resolve("\xE5\xBF\xAB\xE9\x80\x92", Dictionary("")).text, std::string("Express delivery"));
    REQUIRE_EQ(model.CallCount(), static_cast<size_t>(1));
    REQUIRE_EQ(notifications.Count(), 1);
}

TEST_CASE(gloss_model_missing_shows_nothing_and_does_not_loop)
{
    FakeModel model; // 没有任何回答 = 模型没装（Missing）
    Notifications notifications;
    ModelGlossCache cache(model.Infer(), [&] { notifications.Notify(); });
    const GlossResolver resolver(MakeOverlay("chinese\tgloss\n"), &cache);

    REQUIRE(resolver.Resolve("\xE5\xBF\xAB\xE9\x80\x92", Dictionary("")).text.empty());
    REQUIRE(model.WaitForCalls(1));
    std::this_thread::sleep_for(50ms);
    // 冷却期内换个词也不再叩门，更不会通知界面重画。
    REQUIRE(resolver.Resolve("\xE8\x8C\xB6", Dictionary("")).text.empty());
    REQUIRE(resolver.Resolve("\xE5\xBF\xAB\xE9\x80\x92", Dictionary("")).text.empty());
    std::this_thread::sleep_for(50ms);
    REQUIRE_EQ(model.CallCount(), static_cast<size_t>(1));
    REQUIRE_EQ(notifications.Count(), 0);
}

TEST_CASE(gloss_model_cooldown_ends_and_failures_are_negatively_cached)
{
    FakeModel model;
    model.answers["\xE5\xBF\xAB\xE9\x80\x92"] = {Status::Error, ""};
    model.answers["\xE8\x8C\xB6"] = {Status::Ready, "tea"};
    std::mutex clock_mutex;
    Time fake_now = Clock::now();
    const auto advance = [&](std::chrono::seconds by) {
        std::lock_guard lock(clock_mutex);
        fake_now += by;
    };
    Notifications notifications;
    ModelGlossCache cache(
        model.Infer(), [&] { notifications.Notify(); },
        [&] {
            std::lock_guard lock(clock_mutex);
            return fake_now;
        });

    REQUIRE(cache.Lookup("\xE5\xBF\xAB\xE9\x80\x92").empty());
    REQUIRE(model.WaitForCalls(1));
    std::this_thread::sleep_for(50ms);
    // 失败按这个词记一笔，30 秒内不重试；过期后才再问一次。
    REQUIRE(cache.Lookup("\xE5\xBF\xAB\xE9\x80\x92").empty());
    std::this_thread::sleep_for(30ms);
    REQUIRE_EQ(model.CallCount(), static_cast<size_t>(1));
    advance(31s);
    REQUIRE(cache.Lookup("\xE5\xBF\xAB\xE9\x80\x92").empty());
    REQUIRE(model.WaitForCalls(2));
    REQUIRE_EQ(notifications.Count(), 0);

    // 别的词不受影响。
    REQUIRE(cache.Lookup("\xE8\x8C\xB6").empty());
    REQUIRE(notifications.WaitFor(1));
    REQUIRE_EQ(cache.Lookup("\xE8\x8C\xB6"), std::string("tea"));
}

TEST_CASE(gloss_model_only_gets_short_pure_chinese_words)
{
    FakeModel model;
    ModelGlossCache cache(model.Infer(), [] {});
    REQUIRE(ModelGlossCache::Eligible("\xE5\xBF\xAB\xE9\x80\x92"));
    REQUIRE(ModelGlossCache::Eligible("\xE5\xA4\xA9"));
    // 六个字以内；整句、夹杂字母数字或标点的候选不是单词释义。
    REQUIRE(ModelGlossCache::Eligible("\xE4\xB8\x80\xE4\xB8\x80\xE4\xB8\x80\xE4\xB8\x80\xE4\xB8\x80\xE4\xB8\x80"));
    REQUIRE(!ModelGlossCache::Eligible(
        "\xE4\xB8\x80\xE4\xB8\x80\xE4\xB8\x80\xE4\xB8\x80\xE4\xB8\x80\xE4\xB8\x80\xE4\xB8\x80"));
    REQUIRE(!ModelGlossCache::Eligible(""));
    REQUIRE(!ModelGlossCache::Eligible("A\xE8\x82\xA1"));
    REQUIRE(!ModelGlossCache::Eligible("\xE5\xA4\xA9\xEF\xBC\x81"));
    REQUIRE(!ModelGlossCache::Eligible("hello"));

    REQUIRE(cache.Lookup("hello").empty());
    REQUIRE(
        cache.Lookup("\xE4\xB8\x80\xE4\xB8\x80\xE4\xB8\x80\xE4\xB8\x80\xE4\xB8\x80\xE4\xB8\x80\xE4\xB8\x80").empty());
    std::this_thread::sleep_for(50ms);
    REQUIRE_EQ(model.CallCount(), static_cast<size_t>(0));
}

TEST_CASE(gloss_model_output_is_cleaned_to_one_short_english_line)
{
    REQUIRE_EQ(ModelGlossCache::Clean("  The weather.  "), std::string("The weather"));
    REQUIRE_EQ(ModelGlossCache::Clean("Weather."), std::string("Weather"));
    REQUIRE_EQ(ModelGlossCache::Clean("\"tea\"\nsecond line"), std::string("tea"));
    REQUIRE_EQ(ModelGlossCache::Clean("U.S."), std::string("U.S."));
    REQUIRE_EQ(ModelGlossCache::Clean("etc."), std::string("etc"));
    REQUIRE(ModelGlossCache::Clean("").empty());
    REQUIRE(ModelGlossCache::Clean("\xE5\xA4\xA9\xE6\xB0\x94").empty()); // 模型原样吐回汉字
    REQUIRE(ModelGlossCache::Clean(std::string(200, 'a')).empty());
}
