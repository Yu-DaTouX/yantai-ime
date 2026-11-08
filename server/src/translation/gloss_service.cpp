#include "translation/gloss_service.h"

#include "english/english_ime.h"
#include "settings/translation_model.h"
#include "translation/gloss_resolver.h"
#include "utils/common_utils.h"

#include <filesystem>
#include <memory>

namespace translation
{
void InstallEnglishGlossResolver()
{
    GlossOverlay overlay;
    overlay.Load(std::filesystem::path(CommonUtils::get_ime_data_path_w()) / L"assets" / L"dictionary" /
                 L"zh_en_gloss.tsv");

    // 进程内只有一份，故意不析构：模型 DLL 已按设计钉在进程里直到退出，退出时不去 join 一次可能正在跑的推理。
    // 推理完成回调里只让英文线程把最近一次查询重算一遍，由它把缓存里的新释义送回候选窗。
    static ModelGlossCache *const model =
        new ModelGlossCache(LocalInference(translation_model::Directory()), [] { EnglishIme::RefreshTranslations(); });
    static GlossResolver *const resolver = new GlossResolver(std::move(overlay), model);
    EnglishIme::SetGlossResolver(
        [](const std::string &key, const EnglishIme::GlossLookup &user, const EnglishIme::GlossLookup &dictionary) {
            auto answer = resolver->Resolve(key, user, dictionary);
            return EnglishIme::GlossAnswer{std::move(answer.text), answer.hidden};
        });
}
} // namespace translation
