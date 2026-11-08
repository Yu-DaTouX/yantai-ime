#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// 用户词库学习的衰减打分，照 librime（rime/algo/dynamics.h，rime/dict/user_dictionary.cc）的做法：
// 每个学习条目记 commits（累计上屏次数）、dee（衰减累积的「热度」）、tick（最近一次上屏时的全局上屏计数）。
// 全局上屏计数每次上屏加一，所以衰减按「上屏了多少次」算，不按时间：几天不打字词库不会变。
namespace user_dictionary::dynamics
{
// 衰减时间常数，单位是 tick。dee 每过 200 tick 乘 1/e，约 139 次上屏减半（librime 同值）。
constexpr double kDecayTicks = 200.0;
// 衰减后 dee 低于它的学习条目查询时视为遗忘：只上屏一次（dee = 1）的条目约 600 次上屏后遗忘。
constexpr double kForgetDee = 0.05;
// dee 换算成权重倍率时的指数。倍率 = (dee / kForgetDee)^kBoostExponent（dee < 20 段），
// 一次上屏约 ×11，五次约 ×40；到阈值时倍率为 1，所以遗忘前权重平滑回落到出厂词频，没有断崖。
constexpr double kBoostExponent = 0.8;
// 迁移旧调频记录时 dee 的上限，避免把旧词库量级的畸形权重换成过大的热度。
constexpr double kMaxMigratedDee = 100.0;
// 每隔多少次上屏，对全部学习条目做一遍衰减结算（回写权重、遗忘过期条目）。
constexpr std::int64_t kSweepIntervalTicks = 8;
// 结算时，新旧权重相差不到 2% 的条目不回写。
constexpr double kSweepRewriteRatio = 0.02;
// 整句、分段造词落库的音节数上限，对标词库里短语自身的上限。再长的只是这一次输入的产物。
constexpr std::size_t kMaxLearnedPhraseSyllables = 7;

// librime formula_d：d 是这次增加的热度，da / ta 是旧热度与它的 tick，t 是当前 tick。
inline double formula_d(double d, double t, double da, double ta)
{
    return d + da * std::exp((ta - t) / kDecayTicks);
}

// librime formula_p：s 恒为 0（这里只有用户词库一路），u 是 commits / t，t 当前 tick，d 衰减后的 dee。
inline double formula_p(double s, double u, double t, double d)
{
    const double kM = 1 / (1 - std::exp(-0.005));
    const double m = s - (s - u) * std::pow(1 - std::exp(-t / 10000), 10);
    return (d < 20) ? m + (0.5 - m) * (d / kM) : m + (1 - m) * (std::pow(4, (d / kM)) - 1) / 3;
}

// 条目的 dee 衰减到 now 时的值。tick 比 now 新（日志被改乱）时按没有衰减算。
inline double decayed_dee(double dee, std::int64_t entry_tick, std::int64_t now)
{
    if (entry_tick >= now)
        return dee;
    return formula_d(0.0, static_cast<double>(now), dee, static_cast<double>(entry_tick));
}

inline bool forgotten(double current_dee)
{
    return !(current_dee >= kForgetDee);
}

// 衰减后的热度换算成对出厂词频的倍率（≥ 1）：librime 排序用 log(formula_p)，这里取它相对遗忘阈值
// 的差、再乘指数后还原，等价于在对数域里把学习分数加到出厂词频上。
inline double boost(double current_dee, std::int64_t commits, std::int64_t now)
{
    if (forgotten(current_dee))
        return 1.0;
    const double t = static_cast<double>((std::max)(now, std::int64_t{1}));
    const double u = static_cast<double>((std::max)(commits, std::int64_t{1})) / t;
    const double score = std::log(formula_p(0.0, u, t, current_dee)) - std::log(formula_p(0.0, u, t, kForgetDee));
    return std::exp(kBoostExponent * (std::max)(score, 0.0));
}

// 学习条目此刻应写进词库的权重：出厂词频 × 倍率，落在 [1, ceiling]。
inline std::int64_t effective_weight(std::int64_t base_weight, double current_dee, std::int64_t commits,
                                     std::int64_t now, std::int64_t ceiling)
{
    const double raw = static_cast<double>((std::max)(base_weight, std::int64_t{1})) * boost(current_dee, commits, now);
    if (!(raw < static_cast<double>(ceiling)))
        return ceiling;
    return (std::max)(static_cast<std::int64_t>(std::llround(raw)), std::int64_t{1});
}

// 反过来：要让倍率达到 target_boost，dee 应该是多少（二分，boost 对 dee 单调）。迁移旧调频记录用。
inline double dee_for_boost(double target_boost, std::int64_t commits, std::int64_t now)
{
    if (!(target_boost > 1.0))
        return kForgetDee;
    double low = kForgetDee;
    double high = kMaxMigratedDee;
    if (boost(high, commits, now) <= target_boost)
        return high;
    for (int i = 0; i < 60; ++i)
    {
        const double mid = (low + high) / 2;
        (boost(mid, commits, now) < target_boost ? low : high) = mid;
    }
    return high;
}
} // namespace user_dictionary::dynamics
