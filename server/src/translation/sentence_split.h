#pragma once

#include <string>
#include <vector>

namespace translation
{
// One unit handed to the translation engine. Pieces without Chinese (digits, English, symbols) are kept verbatim.
struct Piece
{
    std::wstring text;
    bool translate = false;
    // Whitespace that followed the piece in the source; line breaks are preserved exactly.
    std::wstring gap;
};

struct SplitText
{
    std::wstring lead; // whitespace before the first piece
    std::vector<Piece> pieces;

    size_t TranslatableCount() const;
};

// Longest piece sent in one engine call. A single call is bounded to about a second, so a long sentence is cut
// after a clause mark, and as a last resort at this length.
inline constexpr size_t kMaxPieceChars = 80;

bool IsChineseChar(wchar_t ch);
bool HasChinese(const std::wstring &text);

// Splits on sentence-ending marks (。！？；!?; and ASCII '.' before whitespace) and on line breaks. Concatenating
// lead + text + gap over all pieces reproduces the input exactly.
SplitText SplitSentences(const std::wstring &text);

// Joins pieces back together: translated pieces are replaced by `translated[i]` (aligned with pieces; entries of
// verbatim pieces are ignored) and separated by one space when the source had no gap between them.
std::wstring Assemble(const SplitText &split, const std::vector<std::wstring> &translated);
} // namespace translation
