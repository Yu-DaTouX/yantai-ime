#include "settings/translation_model.h"
#include "settings/zip_archive.h"

#include <set>

namespace translation_model
{
namespace
{
bool InstallArchive(const std::filesystem::path &archive, const std::filesystem::path &directory, std::string &error)
{
    user_data_zip::Reader reader(archive, {128ull << 20, 256ull << 20, 64});
    if (!reader.Open(error))
        return false;
    const std::set<std::string> required{"model.bin",  "config.json",          "shared_vocabulary.json",
                                         "source.spm", "target.spm",           "yantai_translate.dll",
                                         "dnnl.dll",   "libprotobuf-lite.dll", "abseil_dll.dll",
                                         "NOTICE.md"};
    std::set<std::string> present;
    for (const auto &entry : reader.Entries())
    {
        if (!user_data_zip::IsSafeArchivePath(entry.path) || !present.insert(entry.path).second)
        {
            error = "翻译组件包含无效路径";
            return false;
        }
    }
    for (const auto &file : required)
    {
        if (!present.count(file))
        {
            error = "翻译组件缺少必要文件";
            return false;
        }
    }

    const auto staging = directory / ".extracting";
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{staging};
    std::error_code fs_error;
    std::filesystem::remove_all(staging, fs_error);
    for (const auto &entry : reader.Entries())
    {
        const auto target = staging / std::filesystem::u8path(entry.path);
        std::filesystem::create_directories(target.parent_path(), fs_error);
        if (fs_error || !reader.ExtractTo(entry, target, error))
        {
            if (error.empty())
                error = "翻译组件解包失败";
            return false;
        }
    }
    // No receipt exists while these moves run, so Server cannot observe a half-installed model.
    for (const auto &entry : reader.Entries())
    {
        const auto name = std::filesystem::u8path(entry.path);
        const auto target = directory / name;
        std::filesystem::create_directories(target.parent_path(), fs_error);
        if (!fs_error)
            std::filesystem::remove(target, fs_error);
        if (!fs_error)
            std::filesystem::rename(staging / name, target, fs_error);
        if (fs_error)
        {
            error = "翻译组件落位失败，请重启输入法后重试";
            return false;
        }
    }
    return true;
}

constexpr model_download::ModelSpec kSpec{
    kDirectory,
    kReceipt,
    L"github.com",
    L"/Yu-DaTouX/yantai-ime/releases/download/translate-model-v1/translate-zh-en-v1.zip",
    81237882,
    "c18b9d8b9962c1a1eea9b365b70b21d5f559299f35a8a35cec517f6507fe4cf1",
    "yantai-ime translate-model-v1",
    "整句翻译模型与本地 CPU 引擎",
    "模型 CC-BY-4.0 / CTranslate2 MIT；oneDNN、SentencePiece Apache-2.0；桥接代码 GPL-3.0",
    "模型作者 University of Helsinki NLP / OPUS-MT；https://huggingface.co/Helsinki-NLP/opus-mt-zh-en\n"
    "许可：https://creativecommons.org/licenses/by/4.0/ 。model.bin 经 CTranslate2 int8 转换。\n"
    "压缩包及分文件 SHA256 清单见本仓 translation-engine/component.lock.json；完整许可证见 licenses/。",
    L"整句翻译组件 (translate-zh-en-v1.zip)",
    L"*.zip",
    L"选择整句翻译组件 ZIP",
    true,
    InstallArchive};

model_download::ModelDownloader &Downloader()
{
    static model_download::ModelDownloader downloader(kSpec);
    return downloader;
}
} // namespace

std::string SourceUrl()
{
    return Downloader().SourceUrl();
}
model_download::ModelStatus GetModelStatus()
{
    // The settings poll also completes a deferred deletion after Server releases its DLLs.
    // Do not require translation mode to be entered just to finish a previous deletion.
    ResolveDirectory();
    return Downloader().GetStatus();
}
bool VerifyModelFile(const std::filesystem::path &file, std::string &error)
{
    return Downloader().Verify(file, error);
}
bool StartDownload()
{
    return Downloader().StartDownload();
}
bool ImportModel(const std::filesystem::path &source)
{
    return Downloader().Import(source);
}
std::filesystem::path PromptForModelFile(HWND owner)
{
    return Downloader().PromptForFile(owner);
}
bool DeleteModel()
{
    return Downloader().Delete();
}
} // namespace translation_model
