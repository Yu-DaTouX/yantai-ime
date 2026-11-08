#include "translation/translation_worker.h"
#include "translation/local_engine.h"

#include <algorithm>
#include <memory>

namespace translation
{
Worker::Infer LocalInference(std::filesystem::path directory)
{
    // The shared handle is destroyed after Worker joins; do not share this function across workers.
    auto engine = std::make_shared<LocalEngine>();
    return [directory = std::move(directory), engine](const Request &request, const Worker::Cancelled &cancelled) {
        Result result{request.generation, Status::Missing, {}};
        if (cancelled())
            return result;
        std::error_code error;
        if (!std::filesystem::is_regular_file(directory / YT_TRANSLATE_COMPONENT_RECEIPT, error) || error ||
            !LocalEngine::FilesPresent(directory))
        {
            engine->Unload();
            return result;
        }
        if (!engine->loaded() && !engine->Load(directory))
            return result;
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
        return result;
    };
}
} // namespace translation
