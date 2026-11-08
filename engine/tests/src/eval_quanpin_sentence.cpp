// 全拼整句级评测：逐条敲入 tests/data/*.tsv 的输入，按「整句方案」逐个模式统计首选准确率、
// 前 3 命中率、整句行的准确率与每键耗时，并报告进程内存占用。
//
// 不是测试：不断言，也不进 ctest。模式与设置页「整句」一节一一对应（名称按设置页文案）：
//   unigram            Unigram 模型整句 = association.sentence_google（候选窗〔Unigram〕）
//   trigram            Trigram 模型整句 = association.sentence_wordlattice，配 sc.lm（〔Trigram〕）
//   neural_desktop     神经网络整句（效果优先）= sentence_neural_desktop（〔神经D〕）
//   neural_keyboard    神经网络整句（速度优先）= sentence_neural_keyboard（〔神经K〕）
//   context            上文词库重排 = context_candidate_rerank（按上文重排整读音的词库候选）
//   octagram:<id>      octagram 语法整句 = sentence_collocation_enabled + 该模型包（〔万象〕〔八股〕…）
// 组合用 + 连接，例如 unigram+neural_desktop、octagram:wanxiang-lts-zh-hans+neural_desktop。
//
// 数据目录一律用副本（METASEQUOIA_IME_DATA_DIR 指过去），模型也放在副本里：
//   set METASEQUOIA_IME_DATA_DIR=E:\yantai-wt\.bench-data
//   eval_quanpin_sentence engine\tests\data\quanpin_sentence_eval_300.tsv --mode trigram
//                          [--rounds 3] [--timeout 2000] [--settle 80] [--detail]
//                          [--sc-lm <path|none>] [--neural-desktop <path>]
//                          [--gram <id>=<path>] [--context <text>] [--fuzzy=<rules>] [--weight=<w>]
//                          [--phrase-bonus=<log10>] [--phrase-min-weight=<n>]
//
// 口径：
// - 每键耗时只算 handle_character 的同步耗时，逐轮汇总后取 3 轮的中位数；预热轮不计。
// - 神经重排与上文重排是异步的，整句行要等后台算完才出现。等待时间（最后一键到后台结果
//   全部收起）单独记为 ready_ms，轮询口径含重查开销，粗于按键耗时，不并入每键耗时。
//   settle（默认 80 ms，--settle 可调）是「回调停止」的收敛窗；超时（--timeout，默认 2000 ms）
//   当作没等到。
// - 内存取本进程结束该模式后的工作集与峰值工作集；一个进程只跑一个模式时才是该模式的独占值
//   （脚本按模式各起一个进程）。
// - 首选指候选列表第 0 行（用户看到的第一行），前 3 命中指第 0~2 行；sent1 指第一条
//   sentence_association 整句行的准确率，用来把「词库整串命中」与「整句猜对」分开看。
// - 只敲键和重置，从不选词，不写用户日志；仍请指向数据目录的副本，别用正在用的数据目录。
#include <windows.h>
#include <psapi.h>

#include "../../contracts/assets/assets.h"
#include "../../core/data_path.h"
#include "../../core/input_session.h"
#include "../../core/sentence_association_options.h"
#include "../../ngram/language_model.h"
#include "../../ngram/octagram/octagram_gram.h"
#include "../../neural/rescore_worker.h"
#include "../../quanpin/quanpin_utils.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "psapi.lib")

namespace
{

struct Case
{
    std::string input;
    std::string expected;
    std::string category;
    // 可选第 4 列：上文。只有 context 模式用得到，缺省为空（=> 上文重排不生效）。
    std::string context;
};

std::vector<Case> load_cases(const char *path)
{
    std::vector<Case> cases;
    std::ifstream file(path, std::ios::binary);
    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        std::istringstream fields(line);
        Case item;
        std::getline(fields, item.input, '\t');
        std::getline(fields, item.expected, '\t');
        std::getline(fields, item.category, '\t');
        std::getline(fields, item.context, '\t');
        if (!item.input.empty() && !item.expected.empty())
            cases.push_back(std::move(item));
    }
    return cases;
}

double percentile(const std::vector<double> &sorted_values, double p)
{
    if (sorted_values.empty())
        return 0.0;
    const size_t index =
        (std::min)(sorted_values.size() - 1, static_cast<size_t>(p * (sorted_values.size() - 1) + 0.5));
    return sorted_values[index];
}

double median_of(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    return values.size() % 2 == 1 ? values[middle] : (values[middle - 1] + values[middle]) / 2.0;
}

bool has_neural_row(const std::vector<WordItem> &items)
{
    return std::any_of(items.begin(), items.end(), [](const WordItem &item) {
        return item.source == CandidateSource::NeuralDesktop || item.source == CandidateSource::NeuralKeyboard;
    });
}

std::string first_sentence(const std::vector<WordItem> &items)
{
    for (const WordItem &item : items)
    {
        if (item.sentence_association && !item.word.empty())
            return item.word;
    }
    return {};
}

std::string utf8(const std::filesystem::path &path)
{
    return metasequoia::path_to_utf8(path);
}

double megabytes(std::size_t bytes)
{
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

// 一个模式解析出来的实际开关与是否需要等后台重排。
struct Plan
{
    std::string label;
    SentenceAssociationOptions options;
    bool await = false;      // 有异步来源（神经重排 / 上文重排）
    bool neural_row = false; // 只在神经来源时用「整句行出现」当收敛判据
    // 只有上文重排时，空上文的输入压根不会排后台任务，这等不到回调，得直接跳过。
    bool context_only = false;
    int settle_ms = 80; // 回调停止多久算收敛，见 await_sentence
    std::string summary;
};

struct ModelPaths
{
    std::string sc_lm;
    std::string neural_desktop;
    std::map<std::string, std::string> grams;
    std::string gram_for(const std::string &id) const
    {
        const auto found = grams.find(id);
        if (found != grams.end())
            return found->second;
        return utf8(metasequoia::data_directory() / "models" / id / (id + ".gram"));
    }
};

std::optional<Plan> build_plan(const std::string &spec, const ModelPaths &models, std::string &error)
{
    Plan plan;
    plan.label = spec;
    SentenceAssociationOptions &options = plan.options;
    std::vector<std::string> notes;

    std::istringstream parts(spec);
    std::string part;
    while (std::getline(parts, part, '+'))
    {
        if (part == "unigram")
        {
            // 设置页的 Unigram 整句就是 Google 解码器那条（候选窗〔Unigram〕），与 sc.lm 无关。
            options.google = true;
            notes.push_back("unigram=google");
        }
        else if (part == "off")
        {
            // 全部整句来源关闭：词库自己的排序，用作上文重排等改动的对照基线。
            notes.push_back("off");
        }
        else if (part == "trigram")
        {
            if (models.sc_lm.empty())
            {
                error = "trigram 需要 sc.lm（--sc-lm 或数据目录/sc.lm）";
                return std::nullopt;
            }
            if (!ngram::shared_language_model(metasequoia::path_from_utf8(models.sc_lm.c_str())).valid())
            {
                error = "sc.lm 载入失败：" + models.sc_lm;
                return std::nullopt;
            }
            options.word_lattice = true;
            options.language_model = models.sc_lm;
            notes.push_back("trigram=word_lattice+sc.lm");
        }
        else if (part == "neural_desktop")
        {
            if (models.neural_desktop.empty() ||
                !std::filesystem::is_regular_file(metasequoia::path_from_utf8(models.neural_desktop.c_str())))
            {
                error = "neural_desktop 需要 sentence-model-desktop.safetensors（--neural-desktop）";
                return std::nullopt;
            }
            options.neural_desktop = true;
            options.neural_desktop_model = models.neural_desktop;
            notes.push_back("neural_desktop");
        }
        else if (part == "neural_keyboard")
        {
            const auto keyboard = metasequoia::data_directory() / metasequoia::assets::neural_model_keyboard;
            if (!std::filesystem::is_regular_file(keyboard))
            {
                error = "neural_keyboard 需要数据目录里的 sentence-model.safetensors";
                return std::nullopt;
            }
            options.neural_keyboard = true;
            notes.push_back("neural_keyboard");
        }
        else if (part == "context")
        {
            options.context_candidate_rerank = true;
            notes.push_back("context_rerank");
        }
        else if (part.rfind("octagram:", 0) == 0)
        {
            const std::string id = part.substr(9);
            const std::string path = models.gram_for(id);
            if (!std::filesystem::is_regular_file(metasequoia::path_from_utf8(path.c_str())))
            {
                error = "octagram 模型缺失：" + path;
                return std::nullopt;
            }
            if (!gram::shared_gram_db(metasequoia::path_from_utf8(path.c_str()))->valid())
            {
                error = " octagram 模型载入失败：" + path;
                return std::nullopt;
            }
            options.collocation_rerank = true;
            options.collocation_model = path;
            notes.push_back("octagram=" + id);
        }
        else
        {
            error = "未知模式片段：" + part;
            return std::nullopt;
        }
    }
    if (notes.empty())
    {
        error = "空模式";
        return std::nullopt;
    }
    plan.await = options.neural_desktop || options.neural_keyboard || options.context_candidate_rerank;
    // 只有神经来源时才靠「整句行出现」收敛；混着词格 / 万象时代码可能把同句神经行去重掉。
    plan.neural_row = plan.await && !options.word_lattice && !options.google && !options.collocation_rerank;
    plan.context_only = options.context_candidate_rerank && !options.neural_desktop && !options.neural_keyboard;
    plan.summary.clear();
    for (const std::string &note : notes)
        plan.summary += (plan.summary.empty() ? "" : " + ") + note;
    return plan;
}

struct Tally
{
    int total = 0;
    int top1 = 0;
    int top3 = 0;
    int sent1 = 0;
    int empty = 0;
    int not_full_pinyin = 0;
    std::map<std::string, std::pair<int, int>> top1_by_category;
    std::map<std::string, std::pair<int, int>> top3_by_category;
};

struct Round
{
    std::vector<double> key_ms;
    std::vector<double> ready_ms;
};

// 后台重排结果回来时叫醒等待方，口径与 Server 的 ready 回调一致。
std::mutex g_ready_mutex;
std::condition_variable g_ready_cv;
std::atomic<int> g_ready_count{0};

void on_rescore_ready()
{
    {
        const std::lock_guard<std::mutex> lock(g_ready_mutex);
        g_ready_count.fetch_add(1);
    }
    g_ready_cv.notify_all();
}

// 等后台重排收起：每一轮都清掉重排结果表，让每轮都真的重算一次，否则后两轮直接命中缓存、
// 等待时间会量成 0。收敛判据分两种：
// - 只有神经来源时（neural_row 为真），等到它自己的整句行出现即可（最多等到超时）。
// - 与词格 / 万象 / 上文重排混用时，神经行可能因与别家同句而被去重掉，永远等不到；改为
//   「回调停止后候选列表不再变化」：每次回调就失效整句排序并重查，连续 settle_ms 没有新
//   回调即认为收敛。
// 返回值是最后一次重查的时间点（毫秒），也就是整句行到齐的时间。
double await_sentence(metasequoia::InputSession &session, const Plan &plan, int timeout_ms, int settle_ms)
{
    if (!plan.await)
        return 0.0;
    const auto start = std::chrono::steady_clock::now();
    const auto elapsed = [&start]() {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    int seen = 0;
    double last_refresh = 0.0;
    // 如果连一个回调都等不到（模型弃权、没有可排的任务），别死等到超时。
    const int first_wait_ms = (std::max)(1000, timeout_ms / 2);
    for (;;)
    {
        const double used = elapsed();
        if (used >= timeout_ms)
            break;
        if (seen == 0 && used >= first_wait_ms)
            break;
        bool woke = false;
        {
            std::unique_lock<std::mutex> lock(g_ready_mutex);
            woke = g_ready_cv.wait_for(lock, std::chrono::milliseconds(2),
                                       [&seen]() { return g_ready_count.load() > seen; });
            seen = g_ready_count.load();
        }
        if (woke)
        {
            // 结果进了重排表，会话缓存里还是旧顺序：失效整句排序后重查一次。
            session.reset_sentence_cache();
            session.recompute_candidates();
            last_refresh = elapsed();
            if (plan.neural_row && has_neural_row(session.candidates()))
                break;
            continue;
        }
        if (seen > 0 && used - last_refresh >= settle_ms)
            break;
    }
    return last_refresh;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr,
                     "usage: eval_quanpin_sentence <cases.tsv> [repeats] [lattice] [octagram] [weight=<w>] "
                     "[model=<path>] [fuzzy=<rules>]\n"
                     "       eval_quanpin_sentence <cases.tsv> --mode <spec> [--mode <spec>...] [--rounds N]\n"
                     "                             [--timeout ms] [--settle ms] [--detail] [--sc-lm <path|none>]\n"
                     "                             [--neural-desktop <path>] [--gram <id>=<path>]\n"
                     "                             [--context <text>] [--fuzzy=<rules>] [--weight=<w>]\n"
                     "  spec: unigram | trigram | neural_desktop | neural_keyboard | context | "
                     "octagram:<id>, joined with '+'\n");
        return 2;
    }
    const auto cases = load_cases(argv[1]);
    if (cases.empty())
    {
        std::fprintf(stderr, "no cases in %s\n", argv[1]);
        return 2;
    }

    int rounds = 3;
    int timeout_ms = 2000;
    int settle_ms = 80;
    bool settle_given = false;
    bool detail = false;
    std::string context_override;
    bool context_given = false;
    std::string sc_lm_override;
    std::string desktop_override;
    std::vector<std::string> gram_overrides;
    std::vector<std::string> mode_specs;
    metasequoia::FuzzyPinyinOptions fuzzy;
    double collocation_weight = -1.0;
    double phrase_bonus = -1.0;
    long long phrase_min_weight = -1;
    std::string legacy_model;
    bool legacy_lattice = false;
    bool legacy_octagram = false;
    bool legacy_repeats = false;

    for (int i = 2; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto value_after = [&](const char *prefix) { return arg.substr(std::strlen(prefix)); };
        if (arg == "lattice")
            legacy_lattice = true;
        else if (arg == "octagram")
            legacy_octagram = true;
        else if (arg == "--detail")
            detail = true;
        else if (arg == "--mode" && i + 1 < argc)
            mode_specs.emplace_back(argv[++i]);
        else if (arg.rfind("--mode=", 0) == 0)
            mode_specs.push_back(value_after("--mode="));
        else if (arg.rfind("--rounds=", 0) == 0)
            rounds = (std::max)(1, std::atoi(value_after("--rounds=").c_str()));
        else if (arg == "--rounds" && i + 1 < argc)
            rounds = (std::max)(1, std::atoi(argv[++i]));
        else if (arg.rfind("--timeout=", 0) == 0)
            timeout_ms = (std::max)(0, std::atoi(value_after("--timeout=").c_str()));
        else if (arg == "--timeout" && i + 1 < argc)
            timeout_ms = (std::max)(0, std::atoi(argv[++i]));
        else if (arg.rfind("--settle=", 0) == 0)
        {
            settle_ms = (std::max)(0, std::atoi(value_after("--settle=").c_str()));
            settle_given = true;
        }
        else if (arg == "--settle" && i + 1 < argc)
        {
            settle_ms = (std::max)(0, std::atoi(argv[++i]));
            settle_given = true;
        }
        else if (arg.rfind("--sc-lm=", 0) == 0)
            sc_lm_override = value_after("--sc-lm=");
        else if (arg == "--sc-lm" && i + 1 < argc)
            sc_lm_override = argv[++i];
        else if (arg.rfind("--neural-desktop=", 0) == 0)
            desktop_override = value_after("--neural-desktop=");
        else if (arg == "--neural-desktop" && i + 1 < argc)
            desktop_override = argv[++i];
        else if (arg.rfind("--gram=", 0) == 0)
            gram_overrides.push_back(value_after("--gram="));
        else if (arg == "--gram" && i + 1 < argc)
            gram_overrides.push_back(argv[++i]);
        else if (arg.rfind("--context=", 0) == 0)
        {
            context_override = value_after("--context=");
            context_given = true;
        }
        else if (arg == "--context" && i + 1 < argc)
        {
            context_override = argv[++i];
            context_given = true;
        }
        else if (arg.rfind("--phrase-bonus=", 0) == 0)
            phrase_bonus = std::strtod(arg.c_str() + 15, nullptr);
        else if (arg.rfind("--phrase-min-weight=", 0) == 0)
            phrase_min_weight = std::strtoll(arg.c_str() + 20, nullptr, 10);
        else if (arg.rfind("weight=", 0) == 0)
            collocation_weight = std::strtod(arg.c_str() + 7, nullptr);
        else if (arg.rfind("model=", 0) == 0)
            legacy_model = arg.substr(6);
        else if (arg.rfind("fuzzy=", 0) == 0)
            fuzzy.rules = static_cast<std::uint32_t>(std::strtoul(arg.c_str() + 6, nullptr, 0));
        else if (!arg.empty() && arg.front() != '-' && !legacy_repeats &&
                 std::isdigit(static_cast<unsigned char>(arg[0])))
        {
            rounds = (std::max)(1, std::atoi(arg.c_str()));
            legacy_repeats = true;
        }
        else
        {
            std::fprintf(stderr, "ignored argument: %s\n", arg.c_str());
        }
    }

    ModelPaths models;
    models.sc_lm = utf8(metasequoia::data_directory() / metasequoia::assets::language_model);
    if (!sc_lm_override.empty())
        models.sc_lm = sc_lm_override == "none" ? std::string{} : sc_lm_override;
    if (!desktop_override.empty())
        models.neural_desktop = desktop_override;
    else
        models.neural_desktop = utf8(metasequoia::data_directory() / "models" / "neural-desktop" /
                                     metasequoia::assets::neural_model_desktop);
    for (const std::string &override_entry : gram_overrides)
    {
        const size_t equals = override_entry.find('=');
        if (equals == std::string::npos)
        {
            std::fprintf(stderr, "--gram 需要 <id>=<path>：%s\n", override_entry.c_str());
            return 2;
        }
        models.grams[override_entry.substr(0, equals)] = override_entry.substr(equals + 1);
    }

    if (mode_specs.empty())
    {
        // 未指定模式时的默认口径对齐出厂配置：Unigram 整句 + 神经速度档。
        std::string spec = "unigram+neural_keyboard";
        if (legacy_lattice)
            spec = "trigram";
        else if (legacy_octagram)
            spec = "octagram:zh-moqi";
        mode_specs.push_back(spec);
    }

    std::vector<Plan> plans;
    for (const std::string &spec : mode_specs)
    {
        std::string error;
        auto plan = build_plan(spec, models, error);
        if (!plan)
        {
            std::fprintf(stderr, "mode %s: %s\n", spec.c_str(), error.c_str());
            return 3;
        }
        if (collocation_weight >= 0.0)
            plan->options.collocation_weight = collocation_weight;
        if (phrase_bonus >= 0.0)
            plan->options.phrase_bonus = phrase_bonus;
        if (phrase_min_weight >= 0)
            plan->options.phrase_bonus_min_weight = phrase_min_weight;
        plan->settle_ms = settle_ms;
        plans.push_back(std::move(*plan));
    }

    // 六参数默认取 GrammarConfig（万象现行）；METASEQUOIA_IME_COLLOCATION_GRAMMAR 用
    // "max,min,coll,non,weak,rear" 覆盖，便于在真实模型上扫参数而不用改代码。
    if (const char *grammar = std::getenv("METASEQUOIA_IME_COLLOCATION_GRAMMAR"))
    {
        std::istringstream fields(grammar);
        std::string field;
        double values[6] = {0, 0, 0, 0, 0, 0};
        int index = 0;
        while (index < 6 && std::getline(fields, field, ','))
            values[index++] = std::strtod(field.c_str(), nullptr);
        if (index == 6)
        {
            for (Plan &plan : plans)
            {
                plan.options.collocation_grammar.collocation_max_length = static_cast<int>(values[0]);
                plan.options.collocation_grammar.collocation_min_length = static_cast<int>(values[1]);
                plan.options.collocation_grammar.collocation_penalty = values[2];
                plan.options.collocation_grammar.non_collocation_penalty = values[3];
                plan.options.collocation_grammar.weak_collocation_penalty = values[4];
                plan.options.collocation_grammar.rear_penalty = values[5];
            }
        }
    }
    if (!legacy_model.empty())
    {
        for (Plan &plan : plans)
            plan.options.collocation_model = legacy_model;
    }

    // 效果档模型一批 12 条 n-best 要算几百毫秒，中间没有回调。收敛窗必须长过这段空窗，
    // 否则会把前缀那批的旧结果当成最终结果（首选停在静态顺序上，看上去像模型弃权）。
    for (Plan &plan : plans)
    {
        if (!settle_given && plan.options.neural_desktop)
            plan.settle_ms = (std::max)(plan.settle_ms, 400);
    }

    std::printf("data directory: %s\n", utf8(metasequoia::data_directory()).c_str());
    std::printf("cases: %zu   rounds: %d (warm-up excluded)   timeout: %d ms\n", cases.size(), rounds, timeout_ms);
    std::printf("sc.lm: %s\n", models.sc_lm.empty() ? "(unused)" : models.sc_lm.c_str());
    std::printf("neural desktop: %s\n\n", models.neural_desktop.c_str());

    neural::RescoreWorker::instance().set_ready_callback(&on_rescore_ready);

    for (const Plan &plan : plans)
    {
        metasequoia::InputSession session(SchemeType::Quanpin, 0);
        session.set_sentence_association(plan.options);
        session.set_fuzzy_pinyin_options(fuzzy);
        if (context_given)
            session.set_rescoring_context(context_override);

        Tally tally;
        std::vector<Round> measured(static_cast<size_t>(rounds));
        // 第一轮预热，只记后面几轮；神经模型的载入和首次缺页不算在耗时里。
        for (int round = -1; round < rounds; ++round)
        {
            Round *bucket = round < 0 ? nullptr : &measured[static_cast<size_t>(round)];
            for (const Case &item : cases)
            {
                // 轮间清掉后台重排结果表，让每一轮都重新算一遍，等待时间才是真的。
                neural::RescoreWorker::instance().clear();
                {
                    const std::lock_guard<std::mutex> lock(g_ready_mutex);
                    g_ready_count.store(0);
                }
                session.reset_state();
                session.reset_cache();
                if (context_given)
                    session.set_rescoring_context(context_override);
                else
                    session.set_rescoring_context(item.context);

                for (const char character : item.input)
                {
                    const auto start = std::chrono::steady_clock::now();
                    session.handle_character(character);
                    const double ms =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                    if (bucket != nullptr)
                        bucket->key_ms.push_back(ms);
                }
                const bool case_context = context_given ? !context_override.empty() : !item.context.empty();
                const bool await_this = plan.await && (!plan.context_only || case_context);
                const double ready_ms = await_this ? await_sentence(session, plan, timeout_ms, plan.settle_ms) : 0.0;
                if (bucket != nullptr && await_this)
                    bucket->ready_ms.push_back(ready_ms);

                if (round < 0)
                    continue;

                const auto &candidates = session.candidates();
                const std::string head = candidates.empty() ? std::string{} : candidates.front().word;
                const std::string sentence = first_sentence(candidates);
                bool in_top3 = false;
                for (size_t i = 0; i < candidates.size() && i < 3; ++i)
                    in_top3 = in_top3 || candidates[i].word == item.expected;
                const bool pass1 = head == item.expected;
                const bool pass3 = in_top3;
                const bool pass_s = sentence == item.expected;
                if (round == 0)
                {
                    tally.total += 1;
                    tally.top1 += pass1 ? 1 : 0;
                    tally.top3 += pass3 ? 1 : 0;
                    tally.sent1 += pass_s ? 1 : 0;
                    tally.empty += candidates.empty() ? 1 : 0;
                    tally.not_full_pinyin += session.is_all_complete_pure_pinyin() ? 0 : 1;
                    auto &one = tally.top1_by_category[item.category];
                    one.first += pass1 ? 1 : 0;
                    one.second += 1;
                    auto &three = tally.top3_by_category[item.category];
                    three.first += pass3 ? 1 : 0;
                    three.second += 1;
                }
                if (detail)
                    std::printf("%s  %-24s %-8s 1:%s  s:%s  rows:%zu  [%s]\n", pass1 ? "PASS" : "FAIL",
                                item.input.c_str(), item.category.c_str(), head.c_str(), sentence.c_str(),
                                candidates.size(), session.get_pinyin_segmentation_with_cases().c_str());
            }
        }
        session.reset_state();

        std::vector<double> round_mean;
        std::vector<double> round_p95;
        std::vector<double> round_median;
        std::vector<double> round_ready;
        for (Round &round : measured)
        {
            std::sort(round.key_ms.begin(), round.key_ms.end());
            double sum = 0.0;
            for (const double ms : round.key_ms)
                sum += ms;
            round_mean.push_back(round.key_ms.empty() ? 0.0 : sum / static_cast<double>(round.key_ms.size()));
            round_p95.push_back(percentile(round.key_ms, 0.95));
            round_median.push_back(percentile(round.key_ms, 0.5));
            round_ready.push_back(round.ready_ms.empty()
                                      ? 0.0
                                      : median_of(std::vector<double>(round.ready_ms.begin(), round.ready_ms.end())));
        }
        const double key_mean = median_of(round_mean);
        const double key_p95 = median_of(round_p95);
        const double key_median = median_of(round_median);
        const double ready = median_of(round_ready);

        PROCESS_MEMORY_COUNTERS_EX memory{};
        double working_set = 0.0;
        double peak_working_set = 0.0;
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                                 sizeof(memory)))
        {
            working_set = megabytes(memory.WorkingSetSize);
            peak_working_set = megabytes(memory.PeakWorkingSetSize);
        }

        const auto rate = [&tally](int hits) {
            return tally.total == 0 ? 0.0 : 100.0 * static_cast<double>(hits) / static_cast<double>(tally.total);
        };

        std::printf("== %s ==\n", plan.label.c_str());
        std::printf("  %s\n", plan.summary.c_str());
        std::printf("  cases %d  top1 %.1f%%  top3 %.1f%%  sent1 %.1f%%  (empty %d, not-full-pinyin %d)\n", tally.total,
                    rate(tally.top1), rate(tally.top3), rate(tally.sent1), tally.empty, tally.not_full_pinyin);
        for (const auto &[category, counts] : tally.top1_by_category)
        {
            const auto found = tally.top3_by_category.find(category);
            const auto three = found == tally.top3_by_category.end() ? std::pair<int, int>{0, 0} : found->second;
            std::printf("  %-8s top1 %d/%d  top3 %d/%d\n", category.c_str(), counts.first, counts.second, three.first,
                        three.second);
        }
        std::printf("  per key ms: mean %.3f  p95 %.3f  median %.3f   ready ms: %.1f%s\n", key_mean, key_p95,
                    key_median, ready, plan.await && ready > 0 ? "" : " (no background result)");
        std::printf("  working set %.1f MB  peak %.1f MB\n", working_set, peak_working_set);
        std::printf("RESULT\tmode=%s\tcases=%d\ttop1=%.2f\ttop3=%.2f\tsent1=%.2f\tkey_mean=%.4f\tkey_p95=%.4f\t"
                    "key_median=%.4f\tready_ms=%.1f\tws_mb=%.1f\tpeak_ws_mb=%.1f\tempty=%d\tnot_full_pinyin=%d\n\n",
                    plan.label.c_str(), tally.total, rate(tally.top1), rate(tally.top3), rate(tally.sent1), key_mean,
                    key_p95, key_median, ready, working_set, peak_working_set, tally.empty, tally.not_full_pinyin);

        neural::RescoreWorker::instance().clear();
    }
    return 0;
}
