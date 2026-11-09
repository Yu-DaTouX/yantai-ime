#include "translation/sentence_split.h"

namespace translation
{
namespace
{
bool IsLineBreak(wchar_t ch)
{
    return ch == L'\n' || ch == L'\r';
}

bool IsBlank(wchar_t ch)
{
    return ch == L' ' || ch == L'\t' || ch == L'\u3000' || ch == L'\u00A0' || IsLineBreak(ch);
}

bool IsTerminator(const std::wstring &text, size_t index)
{
    switch (text[index])
    {
    case L'。':
    case L'！':
    case L'？':
    case L'；':
    case L'…':
    case L'!':
    case L'?':
    case L';':
        return true;
    case L'.': {
        // "3.14" and "a.b" are not sentence ends.
        const size_t next = index + 1;
        return next >= text.size() || IsBlank(text[next]);
    }
    default:
        return false;
    }
}

bool IsClosing(wchar_t ch)
{
    return ch == L'”' || ch == L'’' || ch == L'）' || ch == L'」' || ch == L'』' || ch == L'》' || ch == L'】' ||
           ch == L'"' || ch == L'\'' || ch == L')';
}

bool IsClauseMark(wchar_t ch)
{
    return ch == L'，' || ch == L'、' || ch == L'：' || ch == L',' || ch == L':';
}

// Cuts an over-long sentence after clause marks, then hard at kMaxPieceChars. Each cut keeps its mark with the
// preceding fragment, so the engine sees the same punctuation the user wrote.
void AppendSentence(std::vector<Piece> &pieces, const std::wstring &sentence, const std::wstring &gap)
{
    if (sentence.size() <= kMaxPieceChars)
    {
        pieces.push_back({sentence, HasChinese(sentence), gap});
        return;
    }
    std::vector<std::wstring> fragments;
    size_t start = 0;
    for (size_t i = 0; i < sentence.size(); ++i)
    {
        const bool mark = IsClauseMark(sentence[i]);
        const size_t length = i + 1 - start;
        if ((mark && length >= kMaxPieceChars / 4) || length >= kMaxPieceChars)
        {
            size_t end = i + 1;
            // Never split a surrogate pair.
            if (!mark && end < sentence.size() && sentence[end - 1] >= 0xD800 && sentence[end - 1] <= 0xDBFF)
                ++end;
            fragments.push_back(sentence.substr(start, end - start));
            start = end;
            i = end - 1;
        }
    }
    if (start < sentence.size())
        fragments.push_back(sentence.substr(start));
    for (size_t i = 0; i < fragments.size(); ++i)
        pieces.push_back({fragments[i], HasChinese(fragments[i]), i + 1 == fragments.size() ? gap : std::wstring()});
}
} // namespace

size_t SplitText::TranslatableCount() const
{
    size_t count = 0;
    for (const Piece &piece : pieces)
        count += piece.translate ? 1 : 0;
    return count;
}

bool IsChineseChar(wchar_t ch)
{
    return (ch >= 0x4E00 && ch <= 0x9FFF) || (ch >= 0x3400 && ch <= 0x4DBF) || (ch >= 0xF900 && ch <= 0xFAFF);
}

bool HasChinese(const std::wstring &text)
{
    for (const wchar_t ch : text)
    {
        if (IsChineseChar(ch))
            return true;
    }
    return false;
}

SplitText SplitSentences(const std::wstring &text)
{
    SplitText result;
    size_t pos = 0;
    while (pos < text.size() && IsBlank(text[pos]))
        ++pos;
    result.lead = text.substr(0, pos);

    while (pos < text.size())
    {
        // A sentence runs to its terminator (with trailing terminators / closing marks) or to the line end.
        size_t end = pos;
        bool terminated = false;
        while (end < text.size() && !IsLineBreak(text[end]))
        {
            if (IsTerminator(text, end))
            {
                ++end;
                while (end < text.size() && (IsTerminator(text, end) || IsClosing(text[end])))
                    ++end;
                terminated = true;
                break;
            }
            ++end;
        }
        (void)terminated;
        size_t bodyEnd = end;
        // Spaces before a line break belong to the gap, not the sentence.
        while (bodyEnd > pos && IsBlank(text[bodyEnd - 1]))
            --bodyEnd;
        size_t gapEnd = end;
        while (gapEnd < text.size() && IsBlank(text[gapEnd]))
            ++gapEnd;
        AppendSentence(result.pieces, text.substr(pos, bodyEnd - pos), text.substr(bodyEnd, gapEnd - bodyEnd));
        pos = gapEnd;
    }
    return result;
}

std::wstring Assemble(const SplitText &split, const std::vector<std::wstring> &translated)
{
    std::wstring out = split.lead;
    for (size_t i = 0; i < split.pieces.size(); ++i)
    {
        const Piece &piece = split.pieces[i];
        if (piece.translate)
        {
            std::wstring english = i < translated.size() ? translated[i] : std::wstring();
            size_t first = 0;
            size_t last = english.size();
            while (first < last && IsBlank(english[first]))
                ++first;
            while (last > first && IsBlank(english[last - 1]))
                --last;
            out.append(english, first, last - first);
        }
        else
        {
            out += piece.text;
        }
        if (piece.gap.empty() && piece.translate && i + 1 < split.pieces.size())
            out.push_back(L' ');
        else
            out += piece.gap;
    }
    return out;
}
} // namespace translation
