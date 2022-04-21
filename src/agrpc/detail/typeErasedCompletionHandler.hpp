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

#ifndef AGRPC_DETAIL_TYPEERASEDCOMPLETIONHANDLER_HPP
#define AGRPC_DETAIL_TYPEERASEDCOMPLETIONHANDLER_HPP

#include "agrpc/detail/allocate.hpp"
#include "agrpc/detail/asioForward.hpp"
#include "agrpc/detail/config.hpp"
#include "agrpc/detail/voidPointerTraits.hpp"

#include <cassert>

AGRPC_NAMESPACE_BEGIN()

namespace detail
{
template <class CompletionHandler>
auto deallocate_completion_handler(CompletionHandler* completion_handler)
{
    auto local_completion_handler{std::move(*completion_handler)};
    auto allocator = asio::get_associated_allocator(local_completion_handler);
    using Allocator = typename std::allocator_traits<decltype(allocator)>::template rebind_alloc<CompletionHandler>;
    detail::deallocate<Allocator>(allocator, completion_handler);
    return local_completion_handler;
}

template <class CompletionHandler, class... Args>
void deallocate_and_invoke(void* data, Args... args)
{
    auto* completion_handler = static_cast<CompletionHandler*>(data);
    auto local_completion_handler = detail::deallocate_completion_handler(completion_handler);
    std::move(local_completion_handler)(std::move(args)...);
}

template <class Executor, class Function, class Allocator>
void post_with_allocator(Executor&& executor, Function&& function, const Allocator& allocator)
{
    asio::execution::execute(
        asio::prefer(asio::require(std::forward<Executor>(executor), asio::execution::blocking_t::never),
                     asio::execution::relationship_t::fork, asio::execution::allocator(allocator)),
        std::forward<Function>(function));
}

template <class CompletionHandler, class... Args>
void post_and_complete(void* ptr, Args... args)
{
    auto* completion_handler = static_cast<CompletionHandler*>(ptr);
    auto local_completion_handler = detail::deallocate_completion_handler(completion_handler);
    auto executor = asio::get_associated_executor(local_completion_handler);
    const auto allocator = asio::get_associated_allocator(local_completion_handler);
    detail::post_with_allocator(
        std::move(executor),
        [ch = std::move(local_completion_handler), args = std::make_tuple(std::move(args)...)]() mutable
        {
            std::apply(std::move(ch), std::move(args));
        },
        allocator);
}

template <class Signature, class VoidPointer>
class BasicTypeErasedCompletionHandler;

template <class Signature>
using AtomicTypeErasedCompletionHandler = detail::BasicTypeErasedCompletionHandler<Signature, std::atomic<void*>>;

template <class Signature>
using TypeErasedCompletionHandler = detail::BasicTypeErasedCompletionHandler<Signature, void*>;

template <class VoidPointer, class R, class... Args>
class BasicTypeErasedCompletionHandler<R(Args...), VoidPointer>
{
  private:
    using Complete = void (*)(void*, Args...);
    using VoidPointerTraits = detail::VoidPointerTraits<VoidPointer>;

    template <class, class>
    friend class BasicTypeErasedCompletionHandler;

  public:
    BasicTypeErasedCompletionHandler() = default;

    BasicTypeErasedCompletionHandler(const BasicTypeErasedCompletionHandler&) = delete;
    BasicTypeErasedCompletionHandler(BasicTypeErasedCompletionHandler&&) = delete;
    BasicTypeErasedCompletionHandler& operator=(const BasicTypeErasedCompletionHandler&) = delete;
    BasicTypeErasedCompletionHandler& operator=(BasicTypeErasedCompletionHandler&&) = delete;

    ~BasicTypeErasedCompletionHandler() { assert(!(*this)); }

    template <class Target, class CompletionHandler>
    void emplace(CompletionHandler&& ch)
    {
        auto allocator{asio::get_associated_allocator(ch)};
        completion_handler = detail::allocate<Target>(allocator, std::forward<CompletionHandler>(ch)).release();
        complete_ = &detail::deallocate_and_invoke<Target, Args...>;
        post_complete_ = &detail::post_and_complete<Target, Args...>;
    }

    auto release() noexcept
    {
        return detail::TypeErasedCompletionHandler<R(Args...)>{this->release_completion_handler(), complete_,
                                                               post_complete_};
    }

    explicit operator bool() const noexcept { return static_cast<bool>(completion_handler); }

    template <class... CompletionArgs>
    R complete(CompletionArgs&&... args) &&
    {
        return complete_(this->release_completion_handler(), std::forward<CompletionArgs>(args)...);
    }

    template <class... CompletionArgs>
    R post_complete(CompletionArgs&&... args) &&
    {
        return post_complete_(this->release_completion_handler(), std::forward<CompletionArgs>(args)...);
    }

  private:
    BasicTypeErasedCompletionHandler(void* completion_handler, Complete complete, Complete post_complete)
        : completion_handler(completion_handler), complete_(complete), post_complete_(post_complete)
    {
    }

    void* release_completion_handler() noexcept { return VoidPointerTraits::exchange(completion_handler, nullptr); }

    VoidPointer completion_handler{};
    Complete complete_;
    Complete post_complete_;
};
}
AGRPC_NAMESPACE_END

#endif  // AGRPC_DETAIL_TYPEERASEDCOMPLETIONHANDLER_HPP
