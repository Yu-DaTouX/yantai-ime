#include "translation/translation_worker.h"
#include "translation/local_engine.h"
#include "engine/english/english_dictionary.h"
#include "engine/core/data_path.h"

#include <algorithm>
#include <memory>

namespace translation
{
Worker::Infer LocalInference(std::filesystem::path directory)
{
    // The shared handle is destroyed after Worker joins; do not share this function across workers.
    auto engine = std::make_shared<LocalEngine>();
    return [directory = std::move(directory), engine, dictionary = std::shared_ptr<EnglishDictionary>{}](
               const Request &request, const Worker::Cancelled &cancelled) mutable {
        const auto missing = [&]() {
            Result fallback{request.generation, Status::Missing, {}};
            if (!dictionary)
                dictionary = std::make_shared<EnglishDictionary>(
                    metasequoia::path_to_utf8(metasequoia::data_directory() / "english.db"), false);
            for (const auto &word : request.words)
            {
                if (cancelled())
                    break;
                auto gloss = word.gloss.empty() ? dictionary->query_english_gloss(word.chinese) : word.gloss;
                if (!fallback.english.empty())
                    fallback.english += " ";
                fallback.english += gloss.empty() ? word.chinese : gloss;
            }
            return fallback;
        };
        Result result{request.generation, Status::Missing, {}};
        if (cancelled())
            return result;
        std::error_code error;
        if (!std::filesystem::is_regular_file(directory / YT_TRANSLATE_COMPONENT_RECEIPT, error) || error ||
            !LocalEngine::FilesPresent(directory))
        {
            engine->Unload();
            return missing();
        }
        if (!engine->loaded() && !engine->Load(directory))
            return missing();
        // First load counts against the same 1 s deadline as token generation.
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(request.deadline - Clock::now()).count();
        if (remaining <= 0)
        {
            result.status = Status::Timeout;
            return result;
        }
        auto callback = [](void *context) -> int32_t {
            return (*static_cast<const Worker::Cancelled *>(context))() ? 1 : 0;
        };
        const int32_t status = engine->Translate(request.chinese, static_cast<uint32_t>((std::min)(remaining, 1000LL)),
                                                 callback, const_cast<Worker::Cancelled *>(&cancelled), result.english);
        result.status = status == YT_TRANSLATE_OK            ? Status::Ready
                        : status == YT_TRANSLATE_TIMEOUT     ? Status::Timeout
                        : status == YT_TRANSLATE_UNAVAILABLE ? Status::Missing
                                                             : Status::Error;
        return result.status == Status::Missing ? missing() : result;
    };
}
} // namespace translation
