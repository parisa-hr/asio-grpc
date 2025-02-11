// Copyright 2022 Dennis Hezel
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef AGRPC_DETAIL_GRPC_CONTEXT_IMPLEMENTATION_IPP
#define AGRPC_DETAIL_GRPC_CONTEXT_IMPLEMENTATION_IPP

#include <agrpc/detail/asio_forward.hpp>
#include <agrpc/detail/grpc_completion_queue_event.hpp>
#include <agrpc/detail/grpc_context.hpp>
#include <agrpc/detail/grpc_context_implementation.hpp>
#include <agrpc/detail/notify_when_done.hpp>
#include <agrpc/detail/operation_base.hpp>
#include <agrpc/grpc_context.hpp>
#include <grpc/support/time.h>
#include <grpcpp/completion_queue.h>

AGRPC_NAMESPACE_BEGIN()

namespace detail
{
inline thread_local const agrpc::GrpcContext* thread_local_grpc_context{};

inline ThreadLocalGrpcContextGuard::ThreadLocalGrpcContextGuard(const agrpc::GrpcContext& grpc_context) noexcept
    : old_context_{GrpcContextImplementation::set_thread_local_grpc_context(&grpc_context)}
{
}

inline ThreadLocalGrpcContextGuard::~ThreadLocalGrpcContextGuard()
{
    GrpcContextImplementation::set_thread_local_grpc_context(old_context_);
}

inline void WorkFinishedOnExitFunctor::operator()() const noexcept { grpc_context_.work_finished(); }

inline StartWorkAndGuard::StartWorkAndGuard(agrpc::GrpcContext& grpc_context) noexcept
    : detail::WorkFinishedOnExit(grpc_context)
{
    grpc_context.work_started();
}

inline bool IsGrpcContextStoppedPredicate::operator()(const agrpc::GrpcContext& grpc_context) const noexcept
{
    return grpc_context.is_stopped();
}

inline bool GrpcContextImplementation::is_shutdown(const agrpc::GrpcContext& grpc_context) noexcept
{
    return grpc_context.shutdown_.load(std::memory_order_relaxed);
}

inline void GrpcContextImplementation::trigger_work_alarm(agrpc::GrpcContext& grpc_context) noexcept
{
    grpc_context.work_alarm_.Set(grpc_context.completion_queue_.get(), GrpcContextImplementation::TIME_ZERO,
                                 GrpcContextImplementation::HAS_REMOTE_WORK_TAG);
}

inline void GrpcContextImplementation::work_started(agrpc::GrpcContext& grpc_context) noexcept
{
    grpc_context.work_started();
}

inline void GrpcContextImplementation::add_remote_operation(agrpc::GrpcContext& grpc_context,
                                                            detail::QueueableOperationBase* op) noexcept
{
    if (grpc_context.remote_work_queue_.enqueue(op))
    {
        GrpcContextImplementation::trigger_work_alarm(grpc_context);
    }
}

inline void GrpcContextImplementation::add_local_operation(agrpc::GrpcContext& grpc_context,
                                                           detail::QueueableOperationBase* op) noexcept
{
    grpc_context.local_work_queue_.push_back(op);
}

inline void GrpcContextImplementation::add_operation(agrpc::GrpcContext& grpc_context,
                                                     detail::QueueableOperationBase* op) noexcept
{
    if (GrpcContextImplementation::running_in_this_thread(grpc_context))
    {
        GrpcContextImplementation::add_local_operation(grpc_context, op);
    }
    else
    {
        GrpcContextImplementation::add_remote_operation(grpc_context, op);
    }
}

inline void GrpcContextImplementation::add_notify_when_done_operation(
    agrpc::GrpcContext& grpc_context, detail::NotfiyWhenDoneSenderImplementation* implementation) noexcept
{
    grpc_context.notify_when_done_list_.push_back(implementation);
}

inline void GrpcContextImplementation::remove_notify_when_done_operation(
    agrpc::GrpcContext& grpc_context, detail::NotfiyWhenDoneSenderImplementation* implementation) noexcept
{
    grpc_context.notify_when_done_list_.remove(implementation);
}

inline void GrpcContextImplementation::deallocate_notify_when_done_list(agrpc::GrpcContext& grpc_context)
{
    auto& list = grpc_context.notify_when_done_list_;
    while (!list.empty())
    {
        auto* implementation = list.pop_front();
        implementation->complete(detail::OperationResult::SHUTDOWN_NOT_OK, grpc_context);
    }
}

inline bool GrpcContextImplementation::running_in_this_thread(const agrpc::GrpcContext& grpc_context) noexcept
{
    return &grpc_context == detail::thread_local_grpc_context;
}

inline const agrpc::GrpcContext* GrpcContextImplementation::set_thread_local_grpc_context(
    const agrpc::GrpcContext* grpc_context) noexcept
{
    return std::exchange(detail::thread_local_grpc_context, grpc_context);
}

inline bool GrpcContextImplementation::move_remote_work_to_local_queue(agrpc::GrpcContext& grpc_context) noexcept
{
    auto remote_work_queue = grpc_context.remote_work_queue_.try_mark_inactive_or_dequeue_all();
    if (remote_work_queue.empty())
    {
        return false;
    }
    grpc_context.local_work_queue_.append(std::move(remote_work_queue));
    return true;
}

inline bool GrpcContextImplementation::process_local_queue(agrpc::GrpcContext& grpc_context,
                                                           detail::InvokeHandler invoke)
{
    bool processed{};
    const auto result =
        detail::InvokeHandler::NO == invoke ? detail::OperationResult::SHUTDOWN_NOT_OK : detail::OperationResult::OK;
    auto queue{std::move(grpc_context.local_work_queue_)};
    while (!queue.empty())
    {
        processed = true;
        detail::WorkFinishedOnExit on_exit{grpc_context};
        auto* operation = queue.pop_front();
        operation->complete(result, grpc_context);
    }
    return processed;
}

inline bool get_next_event(grpc::CompletionQueue* cq, detail::GrpcCompletionQueueEvent& event,
                           ::gpr_timespec deadline) noexcept
{
    return grpc::CompletionQueue::GOT_EVENT == cq->AsyncNext(&event.tag, &event.ok, deadline);
}

inline bool GrpcContextImplementation::handle_next_completion_queue_event(agrpc::GrpcContext& grpc_context,
                                                                          ::gpr_timespec deadline,
                                                                          detail::InvokeHandler invoke)
{
    if (detail::GrpcCompletionQueueEvent event;
        detail::get_next_event(grpc_context.get_completion_queue(), event, deadline))
    {
        if (GrpcContextImplementation::HAS_REMOTE_WORK_TAG == event.tag)
        {
            grpc_context.check_remote_work_ = true;
        }
        else
        {
            const auto result =
                detail::InvokeHandler::NO == invoke
                    ? (event.ok ? detail::OperationResult::SHUTDOWN_OK : detail::OperationResult::SHUTDOWN_NOT_OK)
                    : (event.ok ? detail::OperationResult::OK : detail::OperationResult::NOT_OK);
            detail::process_grpc_tag(event.tag, result, grpc_context);
        }
        return true;
    }
    return false;
}

template <class StopPredicate>
inline bool GrpcContextImplementation::do_one(agrpc::GrpcContext& grpc_context, ::gpr_timespec deadline,
                                              detail::InvokeHandler invoke, StopPredicate stop_predicate)
{
    bool processed{};
    bool check_remote_work = grpc_context.check_remote_work_;
    if (check_remote_work)
    {
        check_remote_work = GrpcContextImplementation::move_remote_work_to_local_queue(grpc_context);
        grpc_context.check_remote_work_ = check_remote_work;
    }
    const bool processed_local_operation = GrpcContextImplementation::process_local_queue(grpc_context, invoke);
    processed = processed || processed_local_operation;
    const bool is_more_completed_work_pending = check_remote_work || !grpc_context.local_work_queue_.empty();
    if (!is_more_completed_work_pending && stop_predicate(grpc_context))
    {
        return processed;
    }
    const bool handled_event = GrpcContextImplementation::handle_next_completion_queue_event(
        grpc_context, is_more_completed_work_pending ? GrpcContextImplementation::TIME_ZERO : deadline, invoke);
    return processed || handled_event;
}

inline bool GrpcContextImplementation::do_one_if_not_stopped(agrpc::GrpcContext& grpc_context, ::gpr_timespec deadline)
{
    if (grpc_context.is_stopped())
    {
        return false;
    }
    return GrpcContextImplementation::do_one(grpc_context, deadline, detail::InvokeHandler::YES);
}

inline bool GrpcContextImplementation::do_one_completion_queue(agrpc::GrpcContext& grpc_context,
                                                               ::gpr_timespec deadline)
{
    return GrpcContextImplementation::handle_next_completion_queue_event(grpc_context, deadline,
                                                                         detail::InvokeHandler::YES);
}

inline bool GrpcContextImplementation::do_one_completion_queue_if_not_stopped(agrpc::GrpcContext& grpc_context,
                                                                              ::gpr_timespec deadline)
{
    if (grpc_context.is_stopped())
    {
        return false;
    }
    return GrpcContextImplementation::handle_next_completion_queue_event(grpc_context, deadline,
                                                                         detail::InvokeHandler::YES);
}

template <class LoopFunction>
inline bool GrpcContextImplementation::process_work(agrpc::GrpcContext& grpc_context, LoopFunction loop_function)
{
    if (GrpcContextImplementation::running_in_this_thread(grpc_context))
    {
        bool processed{};
        while (loop_function(grpc_context))
        {
            processed = true;
        }
        return processed;
    }
    if (grpc_context.outstanding_work_.load(std::memory_order_relaxed) == 0)
    {
        grpc_context.stopped_.store(true, std::memory_order_relaxed);
        return false;
    }
    grpc_context.reset();
    [[maybe_unused]] detail::GrpcContextThreadContext thread_context;
    detail::ThreadLocalGrpcContextGuard guard{grpc_context};
    bool processed{};
    while (loop_function(grpc_context))
    {
        processed = true;
    }
    return processed;
}

inline void process_grpc_tag(void* tag, detail::OperationResult result, agrpc::GrpcContext& grpc_context)
{
    detail::WorkFinishedOnExit on_exit{grpc_context};
    auto* operation = static_cast<detail::OperationBase*>(tag);
    operation->complete(result, grpc_context);
}

inline ::gpr_timespec gpr_timespec_from_now(std::chrono::nanoseconds duration) noexcept
{
    const auto duration_timespec = ::gpr_time_from_nanos(duration.count(), GPR_TIMESPAN);
    const auto timespec = ::gpr_now(GPR_CLOCK_MONOTONIC);
    return ::gpr_time_add(timespec, duration_timespec);
}

inline detail::GrpcContextLocalAllocator get_local_allocator(agrpc::GrpcContext& grpc_context) noexcept
{
    return grpc_context.get_allocator();
}
}

AGRPC_NAMESPACE_END

#endif  // AGRPC_DETAIL_GRPC_CONTEXT_IMPLEMENTATION_IPP
