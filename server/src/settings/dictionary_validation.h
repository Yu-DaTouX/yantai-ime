#pragma once

#include "engine/quanpin/quanpin_query.h"

#include <cstddef>
#include <string>
#include <unordered_map>

namespace SettingsDictionary::Validation
{
// 缺省权重按词库区分。拼音表 2 字词的实测权重 p50=2805、max=9931703；同一个 10000
// 落在拼音的 p50~p60 合理区间。
inline constexpr int kDefaultPinyinImportWeight = 10000;
bool NormalizeFullPinyin(const std::string &input, quanpin::Segments &segments, std::string &normalized,
                         std::size_t expected_syllables = 0);
bool ShouldSkipImportLine(const std::string &line, bool &in_yaml_header);
bool ParseCodedImportLine(const std::string &line, std::string &word, std::string &code, int &weight,
                          std::string &message, int default_weight = kDefaultPinyinImportWeight);
bool QuickPhraseFitsNamedPipe(const std::string &phrase);

// 快捷短语正文里的换行统一成 \n：编辑框里敲出来的是 \n，粘贴进来的可能是 \r\n 或裸 \r。
std::string NormalizeQuickPhraseText(const std::string &text);

// 导入导出走单行 TSV，字段内的换行/Tab/反斜杠写成 \\n、\\t、\\\\；未知转义原样保留，
// 免得手写文件里的正则或路径被吃掉。
std::string EscapeQuickPhraseField(const std::string &text);
std::string UnescapeQuickPhraseField(const std::string &text);
} // namespace SettingsDictionary::Validation
