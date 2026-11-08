#pragma once

#include <string>
#include <vector>

#include "ipc/candidate_translation_policy.h"

// Built once on the candidate worker. Renderers only adapt this presentation data;
// they do not query dictionaries or decide which source gets a badge.
struct CandidateViewItem
{
    std::string text;
    std::string annotation;
    // 来源/状态徽标（☁️/🤖、固定排位 📎）。纯展示字段：D2D 候选窗把 text+badge 拼起来画。
    // page_words 与上屏内容不含徽标。
    std::string badge;
    std::string translation;
    bool fixed_position = false;
    // 这个候选的中译英释义原文（只有中→英方向才填）；候选窗的英文行和 Ctrl+Enter 都从这里取。
    // translation 是给「候选词翻译」显示的格式化文字，和这里的原文不是一回事。
    // 放在 fixed_position 之后：已有的 5 字段聚合初始化（测试里的 CandidateViewItem{...}）保持不变。
    std::string english_gloss;
    // 英文词义清单页上这一项的词性（"n." / "adj."），右对齐小号灰字，不上屏。
    std::string english_pos;
};

inline std::string EscapeCandidateViewHtml(const std::string &text)
{
    std::string result;
    for (char ch : text)
    {
        switch (ch)
        {
        case '&':
            result += "&amp;";
            break;
        case '<':
            result += "&lt;";
            break;
        case '>':
            result += "&gt;";
            break;
        case '"':
            result += "&quot;";
            break;
        default:
            result += ch;
            break;
        }
    }
    return result;
}

// 固定排位的词条补固定徽标：D2D 候选窗原本不显示任何固定标记。复用 badge 的既有展示
// 路径而非新增字段——D2D 拼 text+badge，CandidateViewHtml 生成的候选串同样带上它。
// show/style 由调用方从配置求值后传入：本头是纯展示层，不读配置。样式默认 📎 而非 📌——
// 实心红钉在蓝底词条上像贴了张贴纸，回形针同样是「固定」语义但安静得多。
// 未列出的样式不加徽标，与配置层白名单同宽，手改 TOML 写错也不会渲染出畸形标记。
inline void ApplyFixedPositionBadge(CandidateViewItem &view, bool show, const std::string &style)
{
    if (!view.fixed_position || !show)
        return;
    if (style == "pushpin")
        view.badge += " \xF0\x9F\x93\x8C"; // 📌
    else if (style == "dot")
        view.badge += " \xC2\xB7"; // ·
    else if (style == "paperclip")
        view.badge += " \xF0\x9F\x93\x8E"; // 📎
}

// 词性与分隔符各包一层 span，皮肤用 .cand-pos / .cand-sep 单独配色；没配时继承 .cand-translation。
inline std::string CandidateTranslationHtml(const std::string &translation)
{
    std::string html = "<span class=\"cand-translation\">";
    for (const auto &segment : FanyImeIpc::SegmentTranslationGloss(translation))
    {
        const std::string text = EscapeCandidateViewHtml(segment.text);
        switch (segment.kind)
        {
        case FanyImeIpc::TranslationSegmentKind::PartOfSpeech:
            html += "<span class=\"cand-pos\">" + text + "</span>";
            break;
        case FanyImeIpc::TranslationSegmentKind::Separator:
            html += "<span class=\"cand-sep\">" + text + "</span>";
            break;
        default:
            html += text;
            break;
        }
    }
    return html + "</span>";
}

inline std::string CandidateViewHtml(const CandidateViewItem &item)
{
    std::string html = EscapeCandidateViewHtml(item.text + item.annotation + item.badge);
    if (item.fixed_position)
        html = "<span style=\"color:#379AD3\">" + html + "</span>";
    if (!item.translation.empty())
        html += CandidateTranslationHtml(item.translation);
    return html;
}

// Identity of a rendered page, used to drop duplicate repaints. Items keep raw commas, so a ',' join cannot tell
// ["a,b", "c"] from ["a", "b,c"]; length-prefix each item instead.
inline std::string CandidatePageSignature(const std::vector<CandidateViewItem> &views)
{
    std::string signature;
    for (const CandidateViewItem &view : views)
    {
        const std::string html = CandidateViewHtml(view);
        signature += std::to_string(html.size());
        signature += ':';
        signature += html;
    }
    return signature;
}

struct CandidateFramePageState
{
    int selected_index = 0;
    int page_index = 0;
    bool has_previous_page = false;
    bool has_next_page = false;
};

inline std::wstring CandidateFrameSignature(const std::wstring &preedit, const std::vector<CandidateViewItem> &views,
                                            int caretX, int caretY, const CandidateFramePageState &pageState)
{
    const std::string candidateSignature = CandidatePageSignature(views);
    std::wstring signature = std::to_wstring(preedit.size()) + L":" + preedit;
    signature += L'|';
    signature += std::to_wstring(candidateSignature.size());
    signature += L':';
    // Preserve each signature byte as one wchar_t value.
    // This identity key compares bytes. It does not decode UTF-8 text.
    for (unsigned char byte : candidateSignature)
    {
        signature += static_cast<wchar_t>(byte);
    }
    signature += L'|';
    signature += std::to_wstring(caretX) + L"," + std::to_wstring(caretY);
    signature += L'|';
    signature += std::to_wstring(pageState.selected_index) + L"," + std::to_wstring(pageState.page_index);
    signature += L',';
    signature += pageState.has_previous_page ? L'1' : L'0';
    signature += L',';
    signature += pageState.has_next_page ? L'1' : L'0';
    return signature;
}
