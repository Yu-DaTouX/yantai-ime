#include "tests/includes/test_framework.h"
#include "tests/includes/test_utf8_path.h"
#include "settings/translation_model.h"

#include <Windows.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

namespace
{
class TranslationDataDirectory
{
  public:
    TranslationDataDirectory()
        : root_(test::TempRoot() / ("msime-translation-" + std::to_string(GetCurrentProcessId())))
    {
        wchar_t *old = nullptr;
        size_t length = 0;
        _wdupenv_s(&old, &length, L"METASEQUOIA_IME_DATA_DIR");
        if (old)
        {
            previous_ = old;
            free(old);
        }
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        std::filesystem::create_directories(root_);
        _wputenv_s(L"METASEQUOIA_IME_DATA_DIR", root_.c_str());
    }
    ~TranslationDataDirectory()
    {
        _wputenv_s(L"METASEQUOIA_IME_DATA_DIR", previous_.c_str());
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }
    std::filesystem::path root_;
    std::wstring previous_;
};

void SeedReceipt()
{
    std::filesystem::create_directories(translation_model::Directory());
    std::ofstream(translation_model::Directory() / translation_model::kReceipt) << "approved";
}

bool InstallTinyComponent(const std::filesystem::path &, const std::filesystem::path &directory, std::string &)
{
    std::ofstream(directory / "engine.dll") << "component";
    return true;
}
} // namespace

TEST_CASE(translation_model_layout_and_source_are_fixed)
{
    TranslationDataDirectory isolated;
    REQUIRE_EQ(translation_model::Directory(), isolated.root_ / "models" / "translate-zh-en");
    REQUIRE(translation_model::ResolveDirectory().empty());
    SeedReceipt();
    REQUIRE_EQ(translation_model::ResolveDirectory(), translation_model::Directory());
    REQUIRE_EQ(translation_model::GetModelStatus().state, "ready");
    REQUIRE_EQ(translation_model::SourceUrl(),
               "https://github.com/Yu-DaTouX/yantai-ime/releases/download/translate-model-v1/translate-zh-en-v1.zip");
    REQUIRE(translation_model::DeleteModel());
    REQUIRE_EQ(translation_model::GetModelStatus().state, "absent");
}

TEST_CASE(translation_model_deletion_keeps_marker_until_pinned_dll_can_be_removed)
{
    TranslationDataDirectory isolated;
    SeedReceipt();
    const auto dll = translation_model::Directory() / "engine.dll";
    std::ofstream(dll) << "held";
    const HANDLE held =
        CreateFileW(dll.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(held != INVALID_HANDLE_VALUE);
    const bool deleted = translation_model::DeleteModel();
    const bool pending_before = translation_model::GetModelStatus().delete_pending;
    const bool refused_before = translation_model::ResolveDirectory().empty();
    const bool marker_preserved = translation_model::GetModelStatus().delete_pending;
    CloseHandle(held);
    REQUIRE(deleted);
    REQUIRE(pending_before);
    REQUIRE(refused_before);
    REQUIRE(marker_preserved);
    REQUIRE(translation_model::ResolveDirectory().empty());
    REQUIRE(!std::filesystem::exists(translation_model::Directory()));
    REQUIRE(!translation_model::GetModelStatus().delete_pending);
}

TEST_CASE(translation_model_rejects_unapproved_archive)
{
    TranslationDataDirectory isolated;
    const auto archive = isolated.root_ / "wrong.zip";
    std::ofstream(archive, std::ios::binary) << "not approved";
    std::string error;
    REQUIRE(!translation_model::VerifyModelFile(archive, error));
    REQUIRE(!error.empty());
}

TEST_CASE(translation_model_local_component_round_trip_when_supplied)
{
    // Opt-in local asset test; CI can run all other cases before the placeholder release exists.
    const char *source = std::getenv("MSIME_TRANSLATION_COMPONENT_ARCHIVE");
    if (!source || !*source)
        return;
    TranslationDataDirectory isolated;
    const auto archive = std::filesystem::u8path(source);
    std::string error;
    REQUIRE(translation_model::VerifyModelFile(archive, error));
    REQUIRE(translation_model::ImportModel(archive));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto status = translation_model::GetModelStatus();
    while (status.state == "importing" && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        status = translation_model::GetModelStatus();
    }
    REQUIRE_EQ(status.state, "ready");
    REQUIRE_EQ(translation_model::ResolveDirectory(), translation_model::Directory());
    REQUIRE(std::filesystem::is_regular_file(translation_model::Directory() / "source.spm"));
    REQUIRE(std::filesystem::is_regular_file(translation_model::Directory() / "target.spm"));
    REQUIRE(std::filesystem::is_regular_file(translation_model::Directory() / "yantai_translate.dll"));
    REQUIRE(translation_model::DeleteModel());
    REQUIRE(translation_model::ResolveDirectory().empty());
}

TEST_CASE(component_downloader_publishes_receipt_only_after_verified_extraction)
{
    TranslationDataDirectory isolated;
    static const model_download::ModelSpec spec{"tiny-component",
                                                ".installed",
                                                L"invalid.invalid",
                                                L"/unused",
                                                3,
                                                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                                                "test fixture",
                                                "test",
                                                "MIT",
                                                "",
                                                L"test",
                                                L"*.*",
                                                L"test",
                                                true,
                                                InstallTinyComponent};
    static model_download::ModelDownloader downloader(spec);
    const auto archive = isolated.root_ / "tiny.zip";
    std::ofstream(archive, std::ios::binary) << "abc";
    REQUIRE(downloader.Import(archive));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    auto status = downloader.GetStatus();
    while (status.state == "importing" && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        status = downloader.GetStatus();
    }
    REQUIRE_EQ(status.state, "ready");
    REQUIRE_EQ(std::filesystem::file_size(downloader.ModelFile()), uintmax_t(65));
    REQUIRE(std::filesystem::is_regular_file(downloader.ModelFile().parent_path() / "engine.dll"));
    REQUIRE(downloader.Delete());
}
