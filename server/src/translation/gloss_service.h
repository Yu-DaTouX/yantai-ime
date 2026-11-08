#pragma once

namespace translation
{
// 在 EnglishIme::Start 之前调用：载入随包覆盖表 assets\dictionary\zh_en_gloss.tsv，
// 并把「覆盖表 → english.db → 本地翻译模型」的释义解析接到英文行的后台查询上。
void InstallEnglishGlossResolver();
} // namespace translation
