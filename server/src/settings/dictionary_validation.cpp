#include "dictionary_validation.h"

#include "ipc/ipc_protocol_limits.h"
#include "utils/common_utils.h"
#include "engine/quanpin/quanpin_utils.h"

#include <utf8.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace SettingsDictionary::Validation
{
bool NormalizeFullPinyin(const std::string &input, quanpin::Segments &segments, std::string &normalized,
                         std::size_t expected_syllables)
{
    std::string source;
    std::istringstream stream(input);
    for (std::string part; stream >> part;)
    {
        if (!source.empty() && source.back() != '\'' && part.front() != '\'')
            source.push_back('\'');
        source += part;
    }
    std::transform(source.begin(), source.end(), source.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (source.empty() || source.front() == '\'' || source.back() == '\'' || source.find("''") != std::string::npos)
    {
        return false;
    }

    if (source.find('\'') != std::string::npos)
    {
        segments = quanpin::split_segments(source);
    }
    else
    {
        segments = quanpin::cut_one_piece_min_segments(source, true);
        if (segments.empty())
            return false;
        if (expected_syllables != 0 && segments.size() != expected_syllables)
        {
            const auto alternatives = quanpin::enumerate_complete_segmentations(quanpin::build_syllable_graph(source));
            const auto match = std::find_if(
                alternatives.begin(), alternatives.end(),
                [expected_syllables](const quanpin::Segments &cut) { return cut.size() == expected_syllables; });
            if (match != alternatives.end())
                segments = *match;
        }
    }

    const auto &valid = quanpin::intact_pinyin_set();
    if (segments.empty() || !std::all_of(segments.begin(), segments.end(), [&valid](const std::string &segment) {
            return !segment.empty() && valid.find(segment) != valid.end();
        }))
    {
        return false;
    }

    std::string without_delimiters = quanpin::join_segments(segments);
    without_delimiters.erase(std::remove(without_delimiters.begin(), without_delimiters.end(), '\''),
                             without_delimiters.end());
    std::string source_without_delimiters = source;
    source_without_delimiters.erase(
        std::remove(source_without_delimiters.begin(), source_without_delimiters.end(), '\''),
        source_without_delimiters.end());
    if (without_delimiters != source_without_delimiters)
        return false;

    // 查询会先把 xvan、lue 归一成 xuan、lve 再查库，导入的键也得落在同一种写法上，否则永远查不到。
    for (auto &segment : segments)
        segment = quanpin::canonical_umlaut_spelling(std::move(segment));
    normalized = quanpin::join_segments(segments);
    return true;
}

bool ShouldSkipImportLine(const std::string &line, bool &in_yaml_header)
{
    const auto begin = line.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return true;
    const auto end = line.find_last_not_of(" \t");
    const auto size = end - begin + 1;
    if (size == 3 && line.compare(begin, 3, "---") == 0)
    {
        in_yaml_header = true;
        return true;
    }
    if (size == 3 && line.compare(begin, 3, "...") == 0)
    {
        in_yaml_header = false;
        return true;
    }
    if (in_yaml_header)
        return true;
    return line[begin] == '#';
}

bool ParseCodedImportLine(const std::string &line, std::string &word, std::string &code, int &weight,
                          std::string &message, int default_weight)
{
    const auto trim = [](std::string value) {
        const auto begin = value.find_first_not_of(' ');
        if (begin == std::string::npos)
            return std::string{};
        const auto end = value.find_last_not_of(' ');
        return value.substr(begin, end - begin + 1);
    };

    std::vector<std::string> fields;
    std::string::size_type start = 0;
    for (;;)
    {
        const auto separator = line.find('\t', start);
        fields.push_back(trim(line.substr(start, separator == std::string::npos ? separator : separator - start)));
        if (separator == std::string::npos)
            break;
        start = separator + 1;
    }
    if (fields.size() != 2 && fields.size() != 3)
    {
        message = "格式错误，应为：词语<Tab>编码[<Tab>权重]";
        return false;
    }

    word = fields[0];
    code = fields[1];
    if (word.empty() || code.empty())
    {
        message = "词语和编码不能为空";
        return false;
    }
    if (fields.size() == 2)
    {
        weight = default_weight;
        return true;
    }

    const std::string &weight_text = fields[2];
    if (weight_text.empty() || weight_text.find('=') != std::string::npos)
    {
        weight = default_weight;
        return true;
    }
    if (!std::all_of(weight_text.begin(), weight_text.end(), [](unsigned char ch) { return std::isdigit(ch); }))
    {
        message = "权重必须是非负整数";
        return false;
    }
    try
    {
        weight = std::stoi(weight_text);
        return true;
    }
    catch (...)
    {
        message = "权重数值无效";
        return false;
    }
}

bool QuickPhraseFitsNamedPipe(const std::string &phrase)
{
    try
    {
        return string_to_wstring(phrase).size() <= FanyImePipeLimits::CandidateTextMaxLength;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace SettingsDictionary::Validation
