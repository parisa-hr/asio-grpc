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

#ifndef AGRPC_DETAIL_RPC_CLIENT_CONTEXT_BASE_HPP
#define AGRPC_DETAIL_RPC_CLIENT_CONTEXT_BASE_HPP

#include <agrpc/detail/config.hpp>
#include <agrpc/detail/forward.hpp>
#include <agrpc/detail/tagged_ptr.hpp>
#include <grpcpp/client_context.h>

AGRPC_NAMESPACE_BEGIN()

namespace detail
{
class AutoCancelClientContextRef
{
  public:
    AutoCancelClientContextRef() = default;

    explicit AutoCancelClientContextRef(grpc::ClientContext& context) noexcept : context_(std::addressof(context)) {}

    AutoCancelClientContextRef(const AutoCancelClientContextRef&) = delete;

    AutoCancelClientContextRef(AutoCancelClientContextRef&& other) noexcept : context_(other.context_.clear()) {}

    ~AutoCancelClientContextRef() noexcept { cancel(); }

    AutoCancelClientContextRef& operator=(const AutoCancelClientContextRef&) = delete;

    AutoCancelClientContextRef& operator=(AutoCancelClientContextRef&& other) noexcept
    {
        if (this != &other)
        {
            cancel();
            context_ = other.context_.clear();
        }
        return *this;
    }

    void clear() noexcept { context_.clear(); }

    void cancel() const
    {
        if (auto* const context_ptr = context_.get())
        {
            context_ptr->TryCancel();
        }
    }

    [[nodiscard]] bool is_null() const noexcept { return context_.is_null(); }

    template <std::uintptr_t Bit>
    [[nodiscard]] bool has_bit() const noexcept
    {
        return context_.template has_bit<Bit>();
    }

    template <std::uintptr_t Bit>
    void set_bit() noexcept
    {
        context_.template set_bit<Bit>();
    }

  private:
    detail::AtomicTaggedPtr<grpc::ClientContext> context_{};
};

class RPCClientContextBase
{
  protected:
    RPCClientContextBase() = default;

    explicit RPCClientContextBase(grpc::ClientContext& client_context) : client_context_(client_context) {}

    [[nodiscard]] bool is_finished() const noexcept { return client_context_.is_null(); }

    void set_finished() noexcept { client_context_.clear(); }

    void cancel() const { client_context_.cancel(); }

    [[nodiscard]] bool is_writes_done() const noexcept { return client_context_.has_bit<0>(); }

    void set_writes_done() noexcept { client_context_.set_bit<0>(); }

  private:
    friend detail::RPCCancellationFunction;

    detail::AutoCancelClientContextRef client_context_;
};
}

AGRPC_NAMESPACE_END

#endif  // AGRPC_DETAIL_RPC_CLIENT_CONTEXT_BASE_HPP
