#pragma once

#include <string>

// Pure helpers that map stored config values onto what the settings page offers, kept out of the
// page builders so tests can cover them without a window.
namespace native_settings
{
// frequency_adjustment.mode 仍接受 disabled / pin / halve / linear / promote，但衰减打分学习之后，
// 中文的 promote / halve / linear 效果相同，设置页只提供「开启（promote）/ 一次置顶 / 关闭」三档。
// 老配置里的 halve / linear 以及不认识的值都显示成「开启」，不改动配置里的值。
inline std::string FrequencyModeChoice(const std::string &stored)
{
    if (stored == "disabled" || stored == "pin")
        return stored;
    return "promote";
}
} // namespace native_settings
