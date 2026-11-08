#include "../../engine/contracts/translation_commit.h"
#include <cstdio>
int main()
{
    FanyImeTranslationCommit::Assembly assembly;
    const std::wstring full(3000, L'x');
    for (size_t offset = 0; offset < full.size(); offset += 199)
        if (!assembly.Append(7, full.substr(offset, 199)))
            return 1;
    if (assembly.Take(7) != full || !assembly.Take(7).empty())
        return 1;
    if (!assembly.Append(8, std::wstring(1, wchar_t(0xD83D))) || !assembly.Append(8, std::wstring(1, wchar_t(0xDE00))))
        return 1;
    if (assembly.Take(8).size() != 2)
        return 1;
    if (assembly.Append(0, L"unsolicited"))
        return 1;
    for (uint64_t n = 1; n <= 8; ++n)
        if (!assembly.Append(n, L"x"))
            return 1;
    if (assembly.Append(9, L"overflow"))
        return 1;
    assembly.Clear();
    if (assembly.Append(1, std::wstring(131073, L'x')))
        return 1;
    std::puts("PASS: full translation commit and bounded fragment assembly");
    return 0;
}
