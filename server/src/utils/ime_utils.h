#pragma once
#include <string>

std::wstring GetPreedit();
// 候选窗预编辑，光标处插一个 U+E000。
std::wstring GetPreeditWithCaretMarker();
// 回给 TSF 的行内预编辑：word 是造词前缀，其余是当前分词输入。
std::wstring BuildTsfPreedit(const std::wstring &word, const std::string &raw);
std::wstring GetTsfPreedit();
