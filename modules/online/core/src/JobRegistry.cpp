#include "sonora/online/core/app/JobRegistry.h"

#include <algorithm>
#include <utility>

namespace sonora::online::core::app
{

    void JobRegistry::begin(std::uint64_t token, contracts::SourceKind kind, std::string keywords)
    {
        Job job;
        job.state.token = token;
        job.state.phase = contracts::JobPhase::Loading;
        job.kind = kind;
        job.keywords = std::move(keywords);
        jobs_[token] = std::move(job);
        current_ = token;
    }

    void JobRegistry::setPhase(std::uint64_t token, contracts::JobPhase phase)
    {
        if (Job *job = findWritable(token); job != nullptr)
        {
            job->state.phase = phase;
            ++job->state.revision;
        }
    }

    void JobRegistry::appendRows(std::uint64_t token, std::vector<contracts::OnlineTrackDto> rows, bool hasMore)
    {
        Job *job = findWritable(token);
        if (job == nullptr)
        {
            // 迟到结果：token 早就换人了，直接丢弃（这正是"旧结果不进模型"）。
            return;
        }
        job->hasMore = hasMore;
        job->rows.insert(job->rows.end(), std::make_move_iterator(rows.begin()),
                         std::make_move_iterator(rows.end()));
        job->state.loaded = static_cast<std::int32_t>(job->rows.size());
        job->state.phase = job->rows.empty() ? contracts::JobPhase::Empty : contracts::JobPhase::Ready;
        ++job->state.revision;
    }

    void JobRegistry::fail(std::uint64_t token, contracts::OnlineError error)
    {
        Job *job = findWritable(token);
        if (job == nullptr)
        {
            return;
        }
        job->state.error = std::move(error);
        job->state.phase = job->state.error.code == kernel::ErrorCode::Unavailable
                               ? contracts::JobPhase::Offline
                               : contracts::JobPhase::Error;
        ++job->state.revision;
    }

    contracts::OnlineJobState JobRegistry::state(std::uint64_t token) const
    {
        const auto it = jobs_.find(token);
        if (it == jobs_.end())
        {
            contracts::OnlineJobState idle;
            idle.token = token;
            return idle;
        }
        return it->second.state;
    }

    contracts::OnlinePageDto JobRegistry::page(std::uint64_t token, std::int32_t offset, std::int32_t limit) const
    {
        contracts::OnlinePageDto out;
        out.token = token;
        const auto it = jobs_.find(token);
        if (it == jobs_.end())
        {
            return out;
        }
        const auto &job = it->second;
        out.revision = job.state.revision;
        out.hasMore = job.hasMore;

        const std::int32_t total = static_cast<std::int32_t>(job.rows.size());
        const std::int32_t from = std::clamp<std::int32_t>(offset, 0, total);
        const std::int32_t count = limit <= 0 ? 0 : std::min(limit, total - from);
        out.rows.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i)
        {
            out.rows.push_back(job.rows[static_cast<std::size_t>(from + i)]);
        }
        return out;
    }

    void JobRegistry::forget(std::uint64_t token)
    {
        jobs_.erase(token);
        if (current_ == token)
        {
            current_ = 0;
        }
    }

    JobRegistry::Job *JobRegistry::find(std::uint64_t token)
    {
        const auto it = jobs_.find(token);
        return it == jobs_.end() ? nullptr : &it->second;
    }

    // 只允许改"当前"那一个任务。旧 token 的记录留着给界面读旧快照，但它们的
    // 结果再也进不来——否则"换搜索词后，上一页的行还往新列表里追加"。
    JobRegistry::Job *JobRegistry::findWritable(std::uint64_t token)
    {
        if (token == 0 || token != current_)
        {
            return nullptr;
        }
        return find(token);
    }

} // namespace sonora::online::core::app
