#include "translation/selection_translate_state.h"

namespace translation::selection
{
namespace
{
Tip TipForFailure(FanyImeSelectionTranslate::Failure failure)
{
    using Failure = FanyImeSelectionTranslate::Failure;
    switch (failure)
    {
    case Failure::NoSelection:
        return Tip::SelectFirst;
    case Failure::TooLong:
        return Tip::TooLong;
    case Failure::Composing:
        return Tip::Composing;
    case Failure::HostRejected:
        return Tip::HostRejected;
    case Failure::SelectionChanged:
        return Tip::SelectionChanged;
    default:
        return Tip::Unsupported;
    }
}

bool OnlyBlank(const std::wstring &text)
{
    for (const wchar_t ch : text)
    {
        if (ch != L' ' && ch != L'\t' && ch != L'\n' && ch != L'\r' && ch != L'\u3000' && ch != L'\u00A0')
            return false;
    }
    return true;
}
} // namespace

const wchar_t *TipText(Tip tip)
{
    switch (tip)
    {
    case Tip::SelectFirst:
        return L"先选中要翻译的文字";
    case Tip::Unsupported:
        return L"这个程序不支持读取选中的文字";
    case Tip::NotInstalled:
        return L"翻译组件未安装，请到设置里下载";
    case Tip::NotChinese:
        return L"选中的文字里没有中文";
    case Tip::TooLong:
        return L"选中的文字太长，请分段翻译";
    case Tip::Composing:
        return L"请先完成当前输入再翻译";
    case Tip::Failed:
        return L"翻译失败";
    case Tip::Timeout:
        return L"翻译超时";
    case Tip::SelectionChanged:
        return L"选区已变化，未替换";
    case Tip::HostRejected:
        return L"这个程序不允许替换选中的文字";
    case Tip::CannotRestore:
        return L"选区已变化，无法还原";
    case Tip::None:
        break;
    }
    return L"";
}

ButtonView Machine::view() const
{
    switch (phase_)
    {
    case Phase::Idle:
        return ButtonView::Translate;
    case Phase::Restorable:
        return ButtonView::Restore;
    default:
        return ButtonView::Busy;
    }
}

Step Machine::ToIdle(Tip tip)
{
    const bool wasIdle = phase_ == Phase::Idle;
    phase_ = Phase::Idle;
    split_ = {};
    pending_.clear();
    translated_.clear();
    applyText_.clear();
    done_ = 0;
    total_ = 0;
    return {Action::None, tip, !wasIdle};
}

Step Machine::ToIdleIf(Phase expected, uint32_t job, Tip tip)
{
    if (phase_ != expected || job != job_)
        return {};
    return ToIdle(tip);
}

Step Machine::Click(Time now, bool componentInstalled)
{
    // An expired restore window is gone even if Tick has not run yet.
    Tick(now);
    switch (phase_)
    {
    case Phase::Idle:
        if (!componentInstalled)
            return {Action::None, Tip::NotInstalled, false};
        phase_ = Phase::Reading;
        if (++job_ == 0)
            job_ = 1;
        deadline_ = now + kReadTimeout;
        return {Action::SendRead, Tip::None, true};
    case Phase::Restorable:
        phase_ = Phase::Restoring;
        deadline_ = now + kHostEditTimeout;
        return {Action::SendRestore, Tip::None, true};
    default:
        // Reading / Translating / Applying / Restoring: a second click must not start another job.
        return {};
    }
}

Step Machine::OnSelectionText(uint32_t job, const std::wstring &text, Time now)
{
    if (phase_ != Phase::Reading || job != job_)
        return {};
    if (text.empty() || OnlyBlank(text))
        return ToIdle(Tip::SelectFirst);
    if (text.size() > FanyImeSelectionTranslate::kMaxSelectionChars)
        return ToIdle(Tip::TooLong);
    split_ = SplitSentences(text);
    if (split_.TranslatableCount() == 0)
        return ToIdle(Tip::NotChinese);
    pending_.clear();
    translated_.assign(split_.pieces.size(), std::wstring());
    for (size_t i = 0; i < split_.pieces.size(); ++i)
    {
        if (split_.pieces[i].translate)
            pending_.push_back(i);
    }
    done_ = 0;
    total_ = pending_.size();
    phase_ = Phase::Translating;
    deadline_ = now + kTranslateTimeout;
    return {Action::TranslateNext, Tip::None, true};
}

Step Machine::OnHostFailure(uint32_t job, FanyImeSelectionTranslate::Failure failure)
{
    if ((phase_ != Phase::Reading && phase_ != Phase::Applying && phase_ != Phase::Restoring) || job != job_)
        return {};
    if (phase_ == Phase::Restoring)
        return ToIdle(Tip::CannotRestore);
    return ToIdle(TipForFailure(failure));
}

std::wstring Machine::NextSource() const
{
    if (phase_ != Phase::Translating || pending_.empty())
        return {};
    return split_.pieces[pending_.front()].text;
}

Step Machine::OnPieceTranslated(uint32_t job, EngineStatus status, const std::wstring &english, Time now)
{
    if (phase_ != Phase::Translating || job != job_ || pending_.empty())
        return {};
    switch (status)
    {
    case EngineStatus::Missing:
        return ToIdle(Tip::NotInstalled);
    case EngineStatus::Timeout:
        return ToIdle(Tip::Timeout);
    case EngineStatus::Error:
        return ToIdle(Tip::Failed);
    case EngineStatus::Ready:
        break;
    }
    if (OnlyBlank(english))
        return ToIdle(Tip::Failed);
    translated_[pending_.front()] = english;
    pending_.erase(pending_.begin());
    ++done_;
    if (!pending_.empty())
        return {Action::TranslateNext, Tip::None, true};

    applyText_ = Assemble(split_, translated_);
    if (applyText_.size() > FanyImeSelectionTranslate::kMaxTranslationChars)
        return ToIdle(Tip::TooLong);
    phase_ = Phase::Applying;
    deadline_ = now + kHostEditTimeout;
    return {Action::SendApply, Tip::None, true};
}

Step Machine::OnApplied(uint32_t job, Time now)
{
    if (phase_ != Phase::Applying || job != job_)
        return {};
    phase_ = Phase::Restorable;
    deadline_ = now + kRestoreWindow;
    split_ = {};
    pending_.clear();
    translated_.clear();
    applyText_.clear();
    done_ = 0;
    total_ = 0;
    return {Action::None, Tip::None, true};
}

Step Machine::OnRestored(uint32_t job)
{
    return ToIdleIf(Phase::Restoring, job, Tip::None);
}

Step Machine::OnInvalidated(uint32_t job)
{
    if (phase_ != Phase::Restorable || job != job_)
        return {};
    return ToIdle(Tip::None);
}

Step Machine::OnRouteChanged()
{
    if (phase_ == Phase::Idle)
        return {};
    return ToIdle(Tip::None);
}

Step Machine::Tick(Time now)
{
    switch (phase_)
    {
    case Phase::Idle:
        return {};
    case Phase::Restorable:
        return now >= deadline_ ? ToIdle(Tip::None) : Step{};
    case Phase::Reading:
        // A host (or an older DLL) that never answers is reported as unable to read the selection.
        return now >= deadline_ ? ToIdle(Tip::Unsupported) : Step{};
    case Phase::Translating:
        return now >= deadline_ ? ToIdle(Tip::Timeout) : Step{};
    case Phase::Applying:
        return now >= deadline_ ? ToIdle(Tip::HostRejected) : Step{};
    case Phase::Restoring:
        return now >= deadline_ ? ToIdle(Tip::CannotRestore) : Step{};
    }
    return {};
}
} // namespace translation::selection
