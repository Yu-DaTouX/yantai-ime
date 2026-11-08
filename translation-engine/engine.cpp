#include "translation/engine_api.h"

#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

// CT2's default reader uses narrow paths on Windows. Keep non-ASCII DataDir paths lossless.
class UnicodeModelReader : public ctranslate2::models::ModelReader
{
  public:
    explicit UnicodeModelReader(std::filesystem::path directory) : directory_(std::move(directory))
    {
    }
    std::string get_model_id() const override
    {
        return directory_.u8string();
    }
    std::unique_ptr<std::istream> get_file(const std::string &name, bool binary) override
    {
        const auto path = std::filesystem::u8path(name);
        if (path.has_parent_path() || path.is_absolute())
            return nullptr;
        auto stream = std::make_unique<std::ifstream>(
            directory_ / path, std::ios::in | (binary ? std::ios::binary : std::ios::openmode(0)));
        return *stream ? std::move(stream) : nullptr;
    }

  private:
    std::filesystem::path directory_;
};

void LoadTokenizer(sentencepiece::SentencePieceProcessor &tokenizer, const std::filesystem::path &file)
{
    std::ifstream input(file, std::ios::binary);
    if (!input)
        throw std::runtime_error("tokenizer unavailable");
    std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (!tokenizer.LoadFromSerializedProto(bytes).ok())
        throw std::runtime_error("tokenizer invalid");
}

struct Engine
{
    sentencepiece::SentencePieceProcessor source;
    sentencepiece::SentencePieceProcessor target;
    std::unique_ptr<ctranslate2::Translator> translator;

    explicit Engine(const std::filesystem::path &directory)
    {
        LoadTokenizer(source, directory / "source.spm");
        LoadTokenizer(target, directory / "target.spm");
        ctranslate2::models::ModelLoader loader(std::make_shared<UnicodeModelReader>(directory));
        loader.device = ctranslate2::Device::CPU;
        loader.compute_type = ctranslate2::ComputeType::INT8;
        ctranslate2::ReplicaPoolConfig pool;
        pool.num_threads_per_replica = 2;
        pool.max_queued_batches = 1;
        translator = std::make_unique<ctranslate2::Translator>(loader, pool);
    }
};

int32_t YT_TRANSLATE_CALL Create(const char *directory, void **engine)
{
    if (!engine)
        return YT_TRANSLATE_INVALID_ARGUMENT;
    *engine = nullptr;
    if (!directory)
        return YT_TRANSLATE_INVALID_ARGUMENT;
    try
    {
        *engine = new Engine(std::filesystem::u8path(directory));
        return YT_TRANSLATE_OK;
    }
    catch (...)
    {
        // Never surface exception.what(): upstream errors can contain source text or paths.
        return YT_TRANSLATE_UNAVAILABLE;
    }
}

void YT_TRANSLATE_CALL Destroy(void *engine)
{
    delete static_cast<Engine *>(engine);
}

int32_t YT_TRANSLATE_CALL Translate(void *handle, const char *source, uint32_t source_bytes, uint32_t timeout_ms,
                                    YtTranslateCancelled cancelled, void *context, char *output,
                                    uint32_t output_capacity, uint32_t *output_bytes)
{
    if (output_bytes)
        *output_bytes = 0;
    if (output && output_capacity)
        output[0] = '\0';
    if (!handle || !source || !output || !output_capacity || !output_bytes || source_bytes > 65536 || !timeout_ms)
        return YT_TRANSLATE_INVALID_ARGUMENT;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    auto stopped = [&]() -> int32_t {
        if (cancelled && cancelled(context))
            return YT_TRANSLATE_CANCELLED;
        return Clock::now() >= deadline ? YT_TRANSLATE_TIMEOUT : YT_TRANSLATE_OK;
    };
    try
    {
        if (const auto status = stopped())
            return status;
        auto &engine = *static_cast<Engine *>(handle);
        std::vector<std::string> tokens;
        if (!engine.source.Encode(std::string(source, source_bytes), &tokens).ok())
            return YT_TRANSLATE_ERROR;
        if (tokens.empty())
            return YT_TRANSLATE_OK;
        tokens.emplace_back("</s>");
        // A single request is bounded, regardless of malicious/unusually long input.
        if (tokens.size() > 512)
            return YT_TRANSLATE_ERROR;
        if (const auto status = stopped())
            return status;
        ctranslate2::TranslationOptions options;
        options.beam_size = 1;
        options.max_decoding_length = 256;
        options.callback = [&](ctranslate2::GenerationStepResult) { return stopped() != YT_TRANSLATE_OK; };
        const auto result = engine.translator->translate_batch({tokens}, options, 1);
        if (const auto status = stopped())
            return status;
        if (result.empty() || result[0].hypotheses.empty())
            return YT_TRANSLATE_ERROR;
        std::string english;
        if (!engine.target.Decode(result[0].hypotheses[0], &english).ok())
            return YT_TRANSLATE_ERROR;
        if (english.size() >= output_capacity)
            return YT_TRANSLATE_BUFFER_TOO_SMALL;
        std::memcpy(output, english.data(), english.size());
        output[english.size()] = '\0';
        *output_bytes = static_cast<uint32_t>(english.size());
        return YT_TRANSLATE_OK;
    }
    catch (...)
    {
        return YT_TRANSLATE_ERROR;
    }
}
} // namespace

int32_t YT_TRANSLATE_CALL yt_translate_get_api(uint32_t version, YtTranslateApi *api)
{
    if (version != 1 || !api || api->struct_size != sizeof(YtTranslateApi))
        return YT_TRANSLATE_INVALID_ARGUMENT;
    *api = {sizeof(YtTranslateApi), 1, Create, Destroy, Translate};
    return YT_TRANSLATE_OK;
}
