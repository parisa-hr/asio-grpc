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

#ifndef AGRPC_DETAIL_COMPLETION_HANDLER_RECEIVER_HPP
#define AGRPC_DETAIL_COMPLETION_HANDLER_RECEIVER_HPP

#include <agrpc/detail/asio_association.hpp>
#include <agrpc/detail/config.hpp>
#include <agrpc/detail/memory_resource.hpp>

AGRPC_NAMESPACE_BEGIN()

namespace detail
{
template <class CompletionHandler>
class CompletionHandlerReceiver
{
  public:
    using allocator_type = detail::AssociatedAllocatorT<CompletionHandler>;

    template <class Ch>
    explicit CompletionHandlerReceiver(Ch&& ch) : completion_handler_(static_cast<Ch&&>(ch))
    {
    }

    static void set_done() noexcept {}

    template <class... Args>
    void set_value(Args&&... args) &&
    {
        static_cast<CompletionHandler&&>(completion_handler_)(static_cast<Args&&>(args)...);
    }

    static void set_error(const std::exception_ptr& ep) { std::rethrow_exception(ep); }

    allocator_type get_allocator() const noexcept { return detail::exec::get_allocator(completion_handler_); }

    const CompletionHandler& completion_handler() const noexcept { return completion_handler_; }

  private:
    CompletionHandler completion_handler_;
};
}

AGRPC_NAMESPACE_END

template <class CompletionHandler, class Alloc>
struct agrpc::detail::container::uses_allocator<agrpc::detail::CompletionHandlerReceiver<CompletionHandler>, Alloc>
    : std::false_type
{
};

#if defined(AGRPC_STANDALONE_ASIO) || defined(AGRPC_BOOST_ASIO)

template <class CompletionHandler, class Allocator1>
struct agrpc::asio::associated_allocator<agrpc::detail::CompletionHandlerReceiver<CompletionHandler>, Allocator1>
{
    using type = asio::associated_allocator_t<CompletionHandler, Allocator1>;

    static constexpr decltype(auto) get(const agrpc::detail::CompletionHandlerReceiver<CompletionHandler>& b,
                                        const Allocator1& allocator = Allocator1()) noexcept
    {
        return asio::get_associated_allocator(b.completion_handler(), allocator);
    }
};

#ifdef AGRPC_ASIO_HAS_CANCELLATION_SLOT

template <template <class, class> class Associator, class CompletionHandler, class DefaultCandidate>
struct agrpc::asio::associator<Associator, agrpc::detail::CompletionHandlerReceiver<CompletionHandler>,
                               DefaultCandidate>
{
    using type = typename Associator<CompletionHandler, DefaultCandidate>::type;

    static constexpr decltype(auto) get(const agrpc::detail::CompletionHandlerReceiver<CompletionHandler>& b,
                                        const DefaultCandidate& c = DefaultCandidate()) noexcept
    {
        return Associator<CompletionHandler, DefaultCandidate>::get(b.completion_handler(), c);
    }
};

#endif

#endif

#ifdef AGRPC_ASIO_HAS_SENDER_RECEIVER
#if !defined(BOOST_ASIO_HAS_DEDUCED_SET_DONE_MEMBER_TRAIT) && !defined(ASIO_HAS_DEDUCED_SET_DONE_MEMBER_TRAIT)
template <class CompletionHandler>
struct agrpc::asio::traits::set_done_member<agrpc::detail::CompletionHandlerReceiver<CompletionHandler>>
{
    static constexpr bool is_valid = true;
    static constexpr bool is_noexcept = true;

    using result_type = void;
};
#endif

#if !defined(BOOST_ASIO_HAS_DEDUCED_SET_VALUE_MEMBER_TRAIT) && !defined(ASIO_HAS_DEDUCED_SET_VALUE_MEMBER_TRAIT)
template <class CompletionHandler, class Vs>
struct agrpc::asio::traits::set_value_member<agrpc::detail::CompletionHandlerReceiver<CompletionHandler>, Vs>
{
    static constexpr bool is_valid = true;
    static constexpr bool is_noexcept = false;

    using result_type = void;
};
#endif

#if !defined(BOOST_ASIO_HAS_DEDUCED_SET_ERROR_MEMBER_TRAIT) && !defined(ASIO_HAS_DEDUCED_SET_ERROR_MEMBER_TRAIT)
template <class CompletionHandler, class E>
struct agrpc::asio::traits::set_error_member<agrpc::detail::CompletionHandlerReceiver<CompletionHandler>, E>
{
    static constexpr bool is_valid = true;
    static constexpr bool is_noexcept = false;

    using result_type = void;
};
#endif
#endif

#endif  // AGRPC_DETAIL_COMPLETION_HANDLER_RECEIVER_HPP
