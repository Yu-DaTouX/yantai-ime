#include "session_factory.h"

#include "config/ime_config.h"
#include "engine_input_session.h"
#include "engine/user_dictionary/user_dictionary_journal.h"
#include "utils/common_utils.h"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace
{
std::string ToLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string ResolveEffectiveBackend(std::string configured_backend)
{
    // Old installations may still persist "legacy" or a per-scheme backend name.
    // They are configuration aliases for the shared engine now; keeping a second
    // composition owner is unsafe.
    if (configured_backend.empty() || configured_backend == "legacy" || configured_backend == "engine" ||
        configured_backend == "engine-quanpin" || configured_backend == "engine-shuangpin")
    {
        return "engine-quanpin";
    }

    throw std::runtime_error("Unsupported input session backend: " + configured_backend);
}
} // namespace

std::shared_ptr<IInputSession> CreateInputSessionFromConfig()
{
    // The dictionary writer commits learned weights into both shipped dictionaries
    // while this session (and the dedicated English/mixed workers) query them on
    // the key path; in WAL mode those reads keep the last committed snapshot
    // instead of waiting for the commit. english.db is written by the same writer
    // (frequency adjustments, entered words, settings edits), and its read-only
    // connection fails the whole candidate page with SQLITE_BUSY in rollback mode.
    // An installer upgrade ships fresh rollback-mode files, so this runs at every
    // session creation, not once.
    user_dictionary::enable_dictionary_write_ahead_logs(CommonUtils::get_ime_data_path());
    const std::string backend = DescribeEffectiveInputSessionBackendFromConfig();
    if (backend == "engine-quanpin")
    {
        return std::make_shared<EngineInputSession>(SchemeType::Quanpin);
    }

    throw std::runtime_error("Unsupported effective input session backend: " + backend);
}

std::string DescribeConfiguredInputSessionBackendFromConfig()
{
    return ToLowerAscii(GetConfiguredSessionBackend());
}

std::string DescribeEffectiveInputSessionBackendFromConfig()
{
    return ResolveEffectiveBackend(DescribeConfiguredInputSessionBackendFromConfig());
}
