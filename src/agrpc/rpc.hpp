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

#ifndef AGRPC_AGRPC_RPC_HPP
#define AGRPC_AGRPC_RPC_HPP

#include <agrpc/default_completion_token.hpp>
#include <agrpc/detail/asio_forward.hpp>
#include <agrpc/detail/config.hpp>
#include <agrpc/detail/grpc_initiate.hpp>
#include <agrpc/detail/memory.hpp>
#include <agrpc/detail/rpc.hpp>
#include <agrpc/grpc_executor.hpp>

AGRPC_NAMESPACE_BEGIN()

namespace detail
{
/**
 * @brief Client and server-side function object to start RPCs
 *
 * The examples below are based on the following .proto file:
 *
 * @snippet example.proto example-proto
 *
 * @attention The completion handler created from the completion token that is provided to the functions described below
 * must have an associated executor that refers to a GrpcContext:
 * @snippet server.cpp bind-executor-to-use-awaitable
 *
 * **Per-Operation Cancellation**
 *
 * None. gRPC does not support cancellation of requests.
 */
struct RequestFn
{
    /**
     * @brief Wait for a unary or server-streaming RPC request from a client
     *
     * Unary RPC:
     *
     * @snippet server.cpp request-unary-server-side
     *
     * Server-streaming RPC:
     *
     * @snippet server.cpp request-server-streaming-server-side
     *
     * @param rpc A pointer to the async version of the RPC method. The async version always starts with `Request`.
     * @param service The AsyncService that corresponds to the RPC method. In the examples above the service is:
     * `example::v1::Example::AsyncService`.
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` indicates that the RPC has indeed been started. If it is `false`
     * then the server has been Shutdown before this particular call got matched to an incoming RPC.
     */
    template <class Service, class Request, class Responder, class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(detail::ServerMultiArgRequest<Service, Request, Responder> rpc,
                    detail::TypeIdentityT<Service>& service, grpc::ServerContext& server_context, Request& request,
                    Responder& responder, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::ServerMultiArgRequestInitFunction<Service, Request, Responder>{rpc, service, server_context,
                                                                                   request, responder},
            static_cast<CompletionToken&&>(token));
    }

    /**
     * @brief Wait for a client-streaming or bidirectional-streaming RPC request from a client
     *
     * Client-streaming RPC:
     *
     * @snippet server.cpp request-client-streaming-server-side
     *
     * Bidirectional-streaming RPC:
     *
     * @snippet server.cpp request-bidirectional-streaming-server-side
     *
     * @param rpc A pointer to the async version of the RPC method. The async version always starts with `Request`.
     * @param service The AsyncService that corresponds to the RPC method. In the examples above the service is:
     * `example::v1::Example::AsyncService`.
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` indicates that the RPC has indeed been started. If it is `false`
     * then the server has been Shutdown before this particular call got matched to an incoming RPC.
     */
    template <class Service, class Responder, class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(detail::ServerSingleArgRequest<Service, Responder> rpc, detail::TypeIdentityT<Service>& service,
                    grpc::ServerContext& server_context, Responder& responder, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::ServerSingleArgRequestInitFunction<Service, Responder>{rpc, service, server_context, responder},
            static_cast<CompletionToken&&>(token));
    }

    /**
     * @brief Wait for a generic RPC request from a client
     *
     * This function can be used to wait for a unary, client-streaming, server-streaming or bidirectional-streaming
     * request from a client.
     *
     * Example:
     *
     * @snippet server.cpp request-generic-server-side
     *
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` indicates that the RPC has indeed been started. If it is `false`
     * then the server has been Shutdown before this particular call got matched to an incoming RPC.
     */
    template <class ReaderWriter, class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(grpc::AsyncGenericService& service, grpc::GenericServerContext& server_context,
                    ReaderWriter& reader_writer, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::ServerGenericRequestInitFunction<ReaderWriter>{service, server_context, reader_writer},
            static_cast<CompletionToken&&>(token));
    }

    /**
     * @brief Start a unary request (client-side)
     *
     * Note, this function completes immediately.
     *
     * Example:
     *
     * @snippet client.cpp request-unary-client-side
     *
     * @param rpc A pointer to the async version of the RPC method. The async version starts with `Async`.
     */
    template <class Stub, class DerivedStub, class Request, class Responder>
    auto operator()(detail::ClientUnaryRequest<Stub, Request, Responder> rpc, DerivedStub& stub,
                    grpc::ClientContext& client_context, const Request& request, agrpc::GrpcContext& grpc_context) const
    {
        return (detail::unwrap_unique_ptr(stub).*rpc)(&client_context, request, grpc_context.get_completion_queue());
    }

#if defined(AGRPC_STANDALONE_ASIO) || defined(AGRPC_BOOST_ASIO)
    /**
     * @brief Convenience function for starting a server-streaming request (Async overload)
     *
     * @deprecated Use the PrepareAsync overload.
     *
     * @param rpc A pointer to the async version of the RPC method. The async version starts with `Async`.
     */
    template <class Stub, class DerivedStub, class Request, class Responder,
              class CompletionToken = agrpc::DefaultCompletionToken>
    [[deprecated("Use PrepareAsync overload to avoid race-conditions")]] auto operator()(
        detail::AsyncClientServerStreamingRequest<Stub, Request, Responder> rpc, DerivedStub& stub,
        grpc::ClientContext& client_context, const Request& request, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate_with_payload<std::unique_ptr<Responder>>(
            detail::AsyncClientServerStreamingRequestConvenienceInitFunction<Stub, Request, Responder>{
                rpc, detail::unwrap_unique_ptr(stub), client_context, request},
            static_cast<CompletionToken&&>(token));
    }

    /**
     * @brief Convenience function for starting a server-streaming request (PrepareAsync overload)
     *
     * Sends `std::unique_ptr<grpc::ClientAsyncReader<Response>>` through the completion handler, otherwise
     * identical to `operator()(PrepareAsyncClientServerStreamingRequest, Stub&, ClientContext&, const Request&,
     * Reader&, CompletionToken&&)`
     *
     * Example:
     *
     * @snippet client.cpp request-server-streaming-client-side-alt
     *
     * @param rpc A pointer to the async version of the RPC method. The async version starts with `PrepareAsync`.
     * @param stub The Stub that corresponds to the RPC method. In the example above the stub is:
     * `example::v1::Example::Stub`.
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(std::pair<std::unique_ptr<grpc::ClientAsyncReader<Response>>, bool>)`. `true`
     * indicates that the RPC is going to go to the wire. If it is `false`, it is not going to the wire. This would
     * happen if the channel is either permanently broken or transiently broken but with the fail-fast option.
     *
     * @since 2.0.0
     */
    template <class Stub, class DerivedStub, class Request, class Responder,
              class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(detail::PrepareAsyncClientServerStreamingRequest<Stub, Request, Responder> rpc, DerivedStub& stub,
                    grpc::ClientContext& client_context, const Request& request, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate_with_payload<std::unique_ptr<Responder>>(
            detail::PrepareAsyncClientServerStreamingRequestConvenienceInitFunction<Stub, Request, Responder>{
                rpc, detail::unwrap_unique_ptr(stub), client_context, request},
            static_cast<CompletionToken&&>(token));
    }
#endif

    /**
     * @brief Start a server-streaming request (Async overload)
     *
     * @deprecated Use the PrepareAsync overload.
     *
     * @param rpc A pointer to the async version of the RPC method. The async version starts with `Async`.
     */
    template <class Stub, class DerivedStub, class Request, class Responder,
              class CompletionToken = agrpc::DefaultCompletionToken>
    [[deprecated("Use PrepareAsync overload to avoid race-conditions")]] auto operator()(
        detail::AsyncClientServerStreamingRequest<Stub, Request, Responder> rpc, DerivedStub& stub,
        grpc::ClientContext& client_context, const Request& request, std::unique_ptr<Responder>& reader,
        CompletionToken&& token = {}) const noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::AsyncClientServerStreamingRequestInitFunction<Stub, Request, Responder>{
                rpc, detail::unwrap_unique_ptr(stub), client_context, request, reader},
            static_cast<CompletionToken&&>(token));
    }

    /**
     * @brief Start a server-streaming request (PrepareAsync overload)
     *
     * Example:
     *
     * @snippet client.cpp request-server-streaming-client-side
     *
     * @param rpc A pointer to the async version of the RPC method. The async version always starts with `PrepareAsync`.
     * @param stub The Stub that corresponds to the RPC method. In the example above the stub is:
     * `example::v1::Example::Stub`.
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` indicates that the RPC is going to go to the wire. If it is `false`,
     * it is not going to the wire. This would happen if the channel is either permanently broken or transiently broken
     * but with the fail-fast option.
     */
    template <class Stub, class DerivedStub, class Request, class Responder,
              class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(detail::PrepareAsyncClientServerStreamingRequest<Stub, Request, Responder> rpc, DerivedStub& stub,
                    grpc::ClientContext& client_context, const Request& request, std::unique_ptr<Responder>& reader,
                    CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::PrepareAsyncClientServerStreamingRequestInitFunction<Stub, Request, Responder>{
                rpc, detail::unwrap_unique_ptr(stub), client_context, request, reader},
            static_cast<CompletionToken&&>(token));
    }

#if defined(AGRPC_STANDALONE_ASIO) || defined(AGRPC_BOOST_ASIO)
    /**
     * @brief Convenience function for starting a client-streaming request (Async overload)
     *
     * @deprecated Use the PrepareAsync overload.
     *
     * @param rpc A pointer to the async version of the RPC method. The async version starts with `Async`.
     */
    template <class Stub, class DerivedStub, class Responder, class Response,
              class CompletionToken = agrpc::DefaultCompletionToken>
    [[deprecated("Use PrepareAsync overload to avoid race-conditions")]] auto operator()(
        detail::AsyncClientClientStreamingRequest<Stub, Responder, Response> rpc, DerivedStub& stub,
        grpc::ClientContext& client_context, Response& response, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate_with_payload<std::unique_ptr<Responder>>(
            detail::AsyncClientClientStreamingRequestConvenienceInitFunction<Stub, Responder, Response>{
                rpc, detail::unwrap_unique_ptr(stub), client_context, response},
            static_cast<CompletionToken&&>(token));
    }

    /**
     * @brief Convenience function for starting a client-streaming request (PrepareAsync overload)
     *
     * Sends `std::unique_ptr<grpc::ClientAsyncWriter<Request>>` through the completion handler, otherwise
     * identical to `operator()(PrepareAsyncClientClientStreamingRequest, Stub&, ClientContext&, Writer&, Response&,
     * CompletionToken&&)`
     *
     * Example:
     *
     * @snippet client.cpp request-client-streaming-client-side-alt
     *
     * @param rpc A pointer to the async version of the RPC method. The async version starts with `PrepareAsync`.
     * @param stub The Stub that corresponds to the RPC method. In the example above the stub is:
     * `example::v1::Example::Stub`.
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(std::pair<std::unique_ptr<grpc::ClientAsyncWriter<Request>>, bool>)`. `true`
     * indicates that the RPC is going to go to the wire. If it is `false`, it is not going to the wire. This would
     * happen if the channel is either permanently broken or transiently broken but with the fail-fast option.
     *
     * @since 2.0.0
     */
    template <class Stub, class DerivedStub, class Responder, class Response,
              class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(detail::PrepareAsyncClientClientStreamingRequest<Stub, Responder, Response> rpc, DerivedStub& stub,
                    grpc::ClientContext& client_context, Response& response, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate_with_payload<std::unique_ptr<Responder>>(
            detail::PrepareAsyncClientClientStreamingRequestConvenienceInitFunction<Stub, Responder, Response>{
                rpc, detail::unwrap_unique_ptr(stub), client_context, response},
            static_cast<CompletionToken&&>(token));
    }
#endif

    /**
     * @brief Start a client-streaming request
     *
     * @deprecated Use the PrepareAsync overload.
     *
     * @param rpc A pointer to the async version of the RPC method. The async version starts with `Async`.
     */
    template <class Stub, class DerivedStub, class Responder, class Response,
              class CompletionToken = agrpc::DefaultCompletionToken>
    [[deprecated("Use PrepareAsync overload to avoid race-conditions")]] auto operator()(
        detail::AsyncClientClientStreamingRequest<Stub, Responder, Response> rpc, DerivedStub& stub,
        grpc::ClientContext& client_context, std::unique_ptr<Responder>& writer, Response& response,
        CompletionToken&& token = {}) const noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::AsyncClientClientStreamingRequestInitFunction<Stub, Responder, Response>{
                rpc, detail::unwrap_unique_ptr(stub), client_context, writer, response},
            static_cast<CompletionToken&&>(token));
    }

    /**
     * @brief Start a client-streaming request (PrepareAsync overload)
     *
     * Example:
     *
     * @snippet client.cpp request-client-streaming-client-side
     *
     * @attention Do not use this function with the
     * [initial_metadata_corked](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#af79c64534c7b208594ba8e76021e2696)
     * option set. Call the member function directly instead:
     * @snippet client.cpp request-client-streaming-client-side-corked
     *
     * @param rpc A pointer to the async version of the RPC method. The async version starts with `PrepareAsync`.
     * @param stub The Stub that corresponds to the RPC method. In the example above the stub is:
     * `example::v1::Example::Stub`.
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` indicates that the RPC is going to go to the wire. If it is `false`,
     * it is not going to the wire. This would happen if the channel is either permanently broken or transiently broken
     * but with the fail-fast option.
     *
     * @since 2.0.0
     */
    template <class Stub, class DerivedStub, class Responder, class Response,
              class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(detail::PrepareAsyncClientClientStreamingRequest<Stub, Responder, Response> rpc, DerivedStub& stub,
                    grpc::ClientContext& client_context, std::unique_ptr<Responder>& writer, Response& response,
                    CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::PrepareAsyncClientClientStreamingRequestInitFunction<Stub, Responder, Response>{
                rpc, detail::unwrap_unique_ptr(stub), client_context, writer, response},
            static_cast<CompletionToken&&>(token));
    }

#if defined(AGRPC_STANDALONE_ASIO) || defined(AGRPC_BOOST_ASIO)
    /**
     * @brief Convenience function for starting a bidirectional-streaming request (Async overload)
     *
     * @deprecated Use the PrepareAsync overload.
     *
     * @param rpc A pointer to the async version of the RPC method. The async version starts with `Async`.
     */
    template <class Stub, class DerivedStub, class Responder, class CompletionToken = agrpc::DefaultCompletionToken>
    [[deprecated("Use PrepareAsync overload to avoid race-conditions")]] auto operator()(
        detail::AsyncClientBidirectionalStreamingRequest<Stub, Responder> rpc, DerivedStub& stub,
        grpc::ClientContext& client_context, CompletionToken&& token = {}) const
    {
        return detail::grpc_initiate_with_payload<std::unique_ptr<Responder>>(
            detail::AsyncClientBidirectionalStreamingRequestConvenienceInitFunction<Stub, Responder>{
                rpc, detail::unwrap_unique_ptr(stub), client_context},
            static_cast<CompletionToken&&>(token));
    }

    /**
     * @brief Convenience function for starting a bidirectional-streaming request (PrepareAsync overload)
     *
     * Sends `std::unique_ptr<grpc::ClientAsyncReaderWriter<Request>>` through the completion handler, otherwise
     * identical to `operator()(PrepareAsyncClientBidirectionalStreamingRequest, Stub&, ClientContext&, ReaderWriter&,
     * CompletionToken&&)`
     *
     * Example:
     *
     * @snippet client.cpp request-bidirectional-client-side-alt
     *
     * @param rpc A pointer to the async version of the RPC method. The async version starts with `PrepareAsync`.
     * @param stub The Stub that corresponds to the RPC method. In the example above the stub is:
     * `example::v1::Example::Stub`.
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(std::pair<std::unique_ptr<grpc::ClientAsyncReaderWriter<Request>>, bool>)`. `true`
     * indicates that the RPC is going to go to the wire. If it is `false`, it is not going to the wire. This would
     * happen if the channel is either permanently broken or transiently broken but with the fail-fast option.
     *
     * @since 2.0.0
     */
    template <class Stub, class DerivedStub, class Responder, class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(detail::PrepareAsyncClientBidirectionalStreamingRequest<Stub, Responder> rpc, DerivedStub& stub,
                    grpc::ClientContext& client_context, CompletionToken&& token = {}) const
    {
        return detail::grpc_initiate_with_payload<std::unique_ptr<Responder>>(
            detail::PrepareAsyncClientBidirectionalStreamingRequestConvenienceInitFunction<Stub, Responder>{
                rpc, detail::unwrap_unique_ptr(stub), client_context},
            static_cast<CompletionToken&&>(token));
    }
#endif

    /**
     * @brief Start a bidirectional-streaming request (Async overload)
     *
     * @deprecated Use the PrepareAsync overload.
     *
     * @param rpc A pointer to the async version of the RPC method. The async version starts with `Async`.
     */
    template <class Stub, class DerivedStub, class Responder, class CompletionToken = agrpc::DefaultCompletionToken>
    [[deprecated("Use PrepareAsync overload to avoid race-conditions")]] auto operator()(
        detail::AsyncClientBidirectionalStreamingRequest<Stub, Responder> rpc, DerivedStub& stub,
        grpc::ClientContext& client_context, std::unique_ptr<Responder>& reader_writer,
        CompletionToken&& token = {}) const noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::AsyncClientBidirectionalStreamingRequestInitFunction<Stub, Responder>{
                rpc, detail::unwrap_unique_ptr(stub), client_context, reader_writer},
            static_cast<CompletionToken&&>(token));
    }

    /**
     * @brief Start a bidirectional-streaming request (PrepareAsync overload)
     *
     * Example:
     *
     * @snippet client.cpp request-bidirectional-client-side
     *
     * @attention Do not use this function with the
     * [initial_metadata_corked](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#af79c64534c7b208594ba8e76021e2696)
     * option set. Call the member function directly instead:
     * @snippet client.cpp request-client-bidirectional-client-side-corked
     *
     * @param rpc A pointer to the async version of the RPC method. The async version starts with `PrepareAsync`.
     * @param stub The Stub that corresponds to the RPC method. In the example above the stub is:
     * `example::v1::Example::Stub`.
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` indicates that the RPC is going to go to the wire. If it is `false`,
     * it is not going to the wire. This would happen if the channel is either permanently broken or transiently broken
     * but with the fail-fast option.
     *
     * @since 2.0.0
     */
    template <class Stub, class DerivedStub, class Responder, class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(detail::PrepareAsyncClientBidirectionalStreamingRequest<Stub, Responder> rpc, DerivedStub& stub,
                    grpc::ClientContext& client_context, std::unique_ptr<Responder>& reader_writer,
                    CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::PrepareAsyncClientBidirectionalStreamingRequestInitFunction<Stub, Responder>{
                rpc, detail::unwrap_unique_ptr(stub), client_context, reader_writer},
            static_cast<CompletionToken&&>(token));
    }

    /**
     * @brief Start a generic unary request
     *
     * Note, this function completes immediately.
     *
     * Example:
     *
     * @snippet client.cpp request-generic-unary-client-side
     *
     * @param method The RPC method to call, e.g. "/test.v1.Test/Unary"
     */
    auto operator()(const std::string& method, grpc::GenericStub& stub, grpc::ClientContext& client_context,
                    const grpc::ByteBuffer& request, agrpc::GrpcContext& grpc_context) const
    {
        auto reader = stub.PrepareUnaryCall(&client_context, method, request, grpc_context.get_completion_queue());
        reader->StartCall();
        return reader;
    }

    /**
     * @brief Start a generic streaming request
     *
     * This function can be used to start a generic client-streaming, server-streaming or bidirectional-streaming
     * request.
     *
     * Example:
     *
     * @snippet client.cpp request-generic-streaming-client-side
     *
     * @attention Do not use this function for client-streaming or bidirectional-streaming RPCs with the
     * [initial_metadata_corked](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#af79c64534c7b208594ba8e76021e2696)
     * option set. Call the member function directly instead:
     * @snippet client.cpp request-client-generic-streaming-corked
     *
     * @param method The RPC method to call, e.g. "/test.v1.Test/Unary"
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` indicates that the RPC is going to go to the wire. If it is `false`,
     * it is not going to the wire. This would happen if the channel is either permanently broken or transiently broken
     * but with the fail-fast option.
     */
    template <class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(const std::string& method, grpc::GenericStub& stub, grpc::ClientContext& client_context,
                    std::unique_ptr<grpc::GenericClientAsyncReaderWriter>& reader_writer,
                    CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::ClientGenericStreamingRequestInitFunction{method, stub, client_context, reader_writer},
            static_cast<CompletionToken&&>(token));
    }
};

/**
 * @brief Client and server-side function object to read from streaming RPCs
 *
 * The examples below are based on the following .proto file:
 *
 * @snippet example.proto example-proto
 *
 * @attention The completion handler created from the completion token that is provided to the functions described below
 * must have an associated executor that refers to a GrpcContext:
 * @snippet server.cpp bind-executor-to-use-awaitable
 *
 * **Per-Operation Cancellation**
 *
 * None. Operations will be cancelled when the deadline of the RPC has been reached
 * (see
 * [grpc::ClientContext::set_deadline](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#ad4e16866fee3f6ee5a10efb5be6f4da6))
 * or the call has been cancelled
 * (see
 * [grpc::ClientContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#abd0f6715c30287b75288015eee628984)
 * and
 * [grpc::ServerContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_server_context.html#a88d3a0c3d53e39f38654ce8fba968301)).
 */
struct ReadFn
{
    /**
     * @brief Read from a streaming RPC
     *
     * This is thread-safe with respect to write or writes_done methods. It should not be called concurrently with other
     * streaming APIs on the same stream. It is not meaningful to call it concurrently with another read on the same
     * stream since reads on the same stream are delivered in order (expect for server-side bidirectional streams where
     * the order is undefined).
     *
     * Example server-side client-streaming:
     *
     * @snippet server.cpp read-client-streaming-server-side
     *
     * Example server-side bidirectional-streaming:
     *
     * @snippet server.cpp read-bidirectional-streaming-server-side
     *
     * Example client-side server-streaming:
     *
     * @snippet client.cpp read-server-streaming-client-side
     *
     * Example client-side bidirectional-streaming:
     *
     * @snippet client.cpp read-bidirectional-client-side
     *
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` indicates that a valid message was read. `false` when
     * there will be no more incoming messages, either because the other side has called WritesDone() or the stream has
     * failed (or been cancelled).
     */
    template <class Reader, class Response, class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(Reader& reader, Response& response, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::ReadInitFunction<Response, detail::UnwrapUniquePtrT<Reader>>{detail::unwrap_unique_ptr(reader),
                                                                                 response},
            static_cast<CompletionToken&&>(token));
    }
};

/**
 * @brief Client and server-side function object to write to streaming RPCs
 *
 * The examples below are based on the following .proto file:
 *
 * @snippet example.proto example-proto
 *
 * @attention The completion handler created from the completion token that is provided to the functions described below
 * must have an associated executor that refers to a GrpcContext:
 * @snippet server.cpp bind-executor-to-use-awaitable
 *
 * **Per-Operation Cancellation**
 *
 * None. Operations will be cancelled when the deadline of the RPC has been reached
 * (see
 * [grpc::ClientContext::set_deadline](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#ad4e16866fee3f6ee5a10efb5be6f4da6))
 * or the call has been cancelled
 * (see
 * [grpc::ClientContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#abd0f6715c30287b75288015eee628984)
 * and
 * [grpc::ServerContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_server_context.html#a88d3a0c3d53e39f38654ce8fba968301)).
 */
struct WriteFn
{
    /**
     * @brief Write to a streaming RPC
     *
     * Only one write may be outstanding at any given time. This is thread-safe with respect to `agrpc::read`. gRPC does
     * not take ownership or a reference to `response`, so it is safe to to deallocate once write returns.
     *
     * Example server-side server-streaming:
     *
     * @snippet server.cpp write-server-streaming-server-side
     *
     * Example server-side bidirectional-streaming:
     *
     * @snippet server.cpp write-bidirectional-streaming-server-side
     *
     * Example client-side client-streaming:
     *
     * @snippet client.cpp write-client-streaming-client-side
     *
     * Example client-side bidirectional-streaming:
     *
     * @snippet client.cpp write-bidirectional-client-side
     *
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` means that the data/metadata/status/etc is going to go to the wire.
     * If it is `false`, it is not going to the wire because the call is already dead (i.e., canceled, deadline expired,
     * other side dropped the channel, etc).
     */
    template <class Writer, class Response, class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(Writer& writer, const Response& response, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::WriteInitFunction<Response, detail::UnwrapUniquePtrT<Writer>>{detail::unwrap_unique_ptr(writer),
                                                                                  response},
            static_cast<CompletionToken&&>(token));
    }

    /**
     * @brief Write to a streaming RPC with options
     *
     * WriteOptions options is used to set the write options of this message, otherwise identical to:
     * `operator()(Writer&, const Response&, CompletionToken&&)`
     */
    template <class Writer, class Response, class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(Writer& writer, const Response& response, grpc::WriteOptions options,
                    CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::WriteWithOptionsInitFunction<Response, detail::UnwrapUniquePtrT<Writer>>{
                detail::unwrap_unique_ptr(writer), response, options},
            static_cast<CompletionToken&&>(token));
    }
};

/**
 * @brief Client-side function object to signal WritesDone to streaming RPCs
 *
 * The examples below are based on the following .proto file:
 *
 * @snippet example.proto example-proto
 *
 * @attention The completion handler created from the completion token that is provided to the functions described below
 * must have an associated executor that refers to a GrpcContext:
 * @snippet server.cpp bind-executor-to-use-awaitable
 *
 * **Per-Operation Cancellation**
 *
 * None. Operations will be cancelled when the deadline of the RPC has been reached
 * (see
 * [grpc::ClientContext::set_deadline](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#ad4e16866fee3f6ee5a10efb5be6f4da6))
 * or the call has been cancelled
 * (see
 * [grpc::ClientContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#abd0f6715c30287b75288015eee628984)
 * and
 * [grpc::ServerContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_server_context.html#a88d3a0c3d53e39f38654ce8fba968301)).
 */
struct WritesDoneFn
{
    /**
     * @brief Signal WritesDone to a streaming RPC
     *
     * Signal the client is done with the writes (half-close the client stream). Thread-safe with respect to read.
     *
     * Example client-streaming:
     *
     * @snippet client.cpp writes_done-client-streaming-client-side
     *
     * Example bidirectional-streaming:
     *
     * @snippet client.cpp write_done-bidirectional-client-side
     *
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` means that the data/metadata/status/etc is going to go to the wire.
     * If it is `false`, it is not going to the wire because the call is already dead (i.e., canceled, deadline expired,
     * other side dropped the channel, etc).
     */
    template <class Writer, class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(Writer& writer, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::ClientWritesDoneInitFunction<detail::UnwrapUniquePtrT<Writer>>{detail::unwrap_unique_ptr(writer)},
            static_cast<CompletionToken&&>(token));
    }
};

/**
 * @brief Client and server-side function object to finish RPCs
 *
 * The examples below are based on the following .proto file:
 *
 * @snippet example.proto example-proto
 *
 * @attention The completion handler created from the completion token that is provided to the functions described below
 * must have an associated executor that refers to a GrpcContext:
 * @snippet server.cpp bind-executor-to-use-awaitable
 *
 * **Per-Operation Cancellation**
 *
 * None. Operations will be cancelled when the deadline of the RPC has been reached
 * (see
 * [grpc::ClientContext::set_deadline](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#ad4e16866fee3f6ee5a10efb5be6f4da6))
 * or the call has been cancelled
 * (see
 * [grpc::ClientContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#abd0f6715c30287b75288015eee628984)
 * and
 * [grpc::ServerContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_server_context.html#a88d3a0c3d53e39f38654ce8fba968301)).
 */
struct FinishFn
{
    /**
     * @brief Finish a streaming RPC (client-side)
     *
     * Indicate that the stream is to be finished and request notification for when the call has been ended.
     *
     * Should not be used concurrently with other operations.
     *
     * It is appropriate to call this method exactly once when:
     *
     * @arg All messages from the server have been received (either known implictly, or explicitly because a previous
     * read operation returned `false`).
     * @arg The client side has no more message to send (this can be declared implicitly by calling this method, or
     * explicitly through an earlier call to the writes_done method). (client- and bidirectional-streaming only)
     *
     * The operation will finish when either:
     *
     * @arg All incoming messages have been read and the server has returned a status.
     * @arg The server has returned a non-OK status.
     * @arg The call failed for some reason and the library generated a status.
     *
     * Note that implementations of this method attempt to receive initial metadata from the server if initial metadata
     * has not been received yet.
     *
     * Side effect:
     *
     * @arg The ClientContext associated with the call is updated with possible initial and trailing metadata received
     * from the server.
     * @arg Attempts to fill in the response parameter that was passed to `agrpc::request`. (client-streaming only)
     *
     * Example server-streaming:
     *
     * @snippet client.cpp finish-server-streaming-client-side
     *
     * Example client-streaming:
     *
     * @snippet client.cpp finish-client-streaming-client-side
     *
     * Example bidirectional-streaming:
     *
     * @snippet client.cpp finish-bidirectional-client-side
     *
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. The bool should always be `true`.
     */
    template <class Responder, class CompletionToken = agrpc::DefaultCompletionToken,
              class = std::enable_if_t<!detail::FinishInitFunction<detail::UnwrapUniquePtrT<Responder>>::IS_CONST>>
    auto operator()(Responder& responder, grpc::Status& status, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::FinishInitFunction<detail::UnwrapUniquePtrT<Responder>>{detail::unwrap_unique_ptr(responder),
                                                                            status},
            static_cast<CompletionToken&&>(token));
    }

    /**
     * @brief Finish a streaming RPC (server-side)
     *
     * Indicate that the stream is to be finished with a certain status code. Should not be used concurrently with other
     * operations.
     *
     * It is appropriate to call this method when either:
     *
     * @arg All messages from the client have been received (either known implictly, or explicitly because a previous
     * read operation returned `false`).
     * @arg It is desired to end the call early with some non-OK status code.
     *
     * This operation will end when the server has finished sending out initial metadata (if not sent already) and
     * status, or if some failure occurred when trying to do so.
     *
     * The ServerContext associated with the call is used for sending trailing (and initial if not already
     * sent) metadata to the client. There are no restrictions to the code of status, it may be non-OK. gRPC does not
     * take ownership or a reference to status, so it is safe to to deallocate once finish returns.
     *
     * Example server-side server-streaming:
     *
     * @snippet server.cpp finish-server-streaming-server-side
     *
     * Example server-side bidirectional-streaming:
     *
     * @snippet server.cpp finish-bidirectional-streaming-server-side
     *
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` means that the data/metadata/status/etc is going to go to the wire.
     * If it is `false`, it is not going to the wire because the call is already dead (i.e., canceled, deadline expired,
     * other side dropped the channel, etc).
     */
    template <class Responder, class CompletionToken = agrpc::DefaultCompletionToken,
              class = std::enable_if_t<detail::FinishInitFunction<detail::UnwrapUniquePtrT<Responder>>::IS_CONST>>
    auto operator()(Responder& responder, const grpc::Status& status, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::FinishInitFunction<detail::UnwrapUniquePtrT<Responder>>{detail::unwrap_unique_ptr(responder),
                                                                            status},
            static_cast<CompletionToken&&>(token));
    }

    /**
     * @brief Finish a unary RPC (client-side)
     *
     * Receive the server's response message and final status for the call.
     *
     * This operation will finish when either:
     *
     * @arg The server's response message and status have been received.
     * @arg The server has returned a non-OK status (no message expected in this case).
     * @arg The call failed for some reason and the library generated a non-OK status.
     *
     * Side effect:
     *
     * @arg The ClientContext associated with the call is updated with possible initial and trailing metadata sent from
     * the server.
     *
     * Example unary:
     *
     * @snippet client.cpp finish-unary-client-side
     *
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. The bool should always be `true`.
     */
    template <
        class Responder, class CompletionToken = agrpc::DefaultCompletionToken,
        class = std::enable_if_t<!detail::FinishWithMessageInitFunction<detail::UnwrapUniquePtrT<Responder>>::IS_CONST>>
    auto operator()(
        Responder& responder,
        typename detail::FinishWithMessageInitFunction<detail::UnwrapUniquePtrT<Responder>>::Message& message,
        grpc::Status& status, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::FinishWithMessageInitFunction<detail::UnwrapUniquePtrT<Responder>>{
                detail::unwrap_unique_ptr(responder), message, status},
            static_cast<CompletionToken&&>(token));
    }

    /**
     * @brief Finish a unary/streaming RPC (server-side)
     *
     * Indicate that the RPC is to be finished and request notification when the server has sent the appropriate
     * signals to the client to end the call. Should not be used concurrently with other operations.
     *
     * Side effect:
     *
     * @arg Also sends initial metadata if not already sent (using the ServerContext associated with the call).
     *
     * @note If status has a non-OK code, then message will not be sent, and the client will receive only the status
     * with possible trailing metadata.
     *
     * gRPC does not take ownership or a reference to message and status, so it is safe to deallocate once finish
     * returns.
     *
     * Example client-streaming:
     *
     * @snippet server.cpp finish-client-streaming-server-side
     *
     * Example unary:
     *
     * @snippet server.cpp finish-unary-server-side
     *
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` means that the data/metadata/status/etc is going to go to the wire.
     * If it is `false`, it is not going to the wire because the call is already dead (i.e., canceled, deadline expired,
     * other side dropped the channel, etc).
     */
    template <
        class Responder, class CompletionToken = agrpc::DefaultCompletionToken,
        class = std::enable_if_t<detail::FinishWithMessageInitFunction<detail::UnwrapUniquePtrT<Responder>>::IS_CONST>>
    auto operator()(
        Responder& responder,
        const typename detail::FinishWithMessageInitFunction<detail::UnwrapUniquePtrT<Responder>>::Message& message,
        const grpc::Status& status, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::FinishWithMessageInitFunction<detail::UnwrapUniquePtrT<Responder>>{
                detail::unwrap_unique_ptr(responder), message, status},
            static_cast<CompletionToken&&>(token));
    }
};

/**
 * @brief Function object to coalesce write and send trailing metadata of streaming RPCs
 *
 * The examples below are based on the following .proto file:
 *
 * @snippet example.proto example-proto
 *
 * @attention The completion handler created from the completion token that is provided to the functions described below
 * must have an associated executor that refers to a GrpcContext:
 * @snippet server.cpp bind-executor-to-use-awaitable
 *
 * **Per-Operation Cancellation**
 *
 * None. Operations will be cancelled when the deadline of the RPC has been reached
 * (see
 * [grpc::ClientContext::set_deadline](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#ad4e16866fee3f6ee5a10efb5be6f4da6))
 * or the call has been cancelled
 * (see
 * [grpc::ClientContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#abd0f6715c30287b75288015eee628984)
 * and
 * [grpc::ServerContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_server_context.html#a88d3a0c3d53e39f38654ce8fba968301)).
 */
struct WriteLastFn
{
    /**
     * @brief Coalesce write and send trailing metadata
     *
     * Clients:
     * Perform `write` and `writes_done` in a single step.
     *
     * Servers:
     * `write_last` buffers the response. The writing of response is held
     * until `finish` is called, where response and trailing metadata are coalesced
     * and write is initiated. Note that `write_last` can only buffer response up to
     * the flow control window size. If response size is larger than the window
     * size, it will be sent on wire without buffering.
     *
     * gRPC does not take ownership or a reference to the message, so it is safe to
     * to deallocate once `write_last` returns.
     *
     * @attention For server-side RPCs this function does not complete until `finish` is called unless the
     * initial metadata has already been send to the client, e.g. by an earlier call to `write` or
     * `send_initial_metadata`.
     *
     * Example server-side server-streaming:
     *
     * @snippet server.cpp write_last-server-streaming-server-side
     *
     * Example server-side bidirectional-streaming:
     *
     * @snippet server.cpp write_last-bidirectional-streaming-server-side
     *
     * Example client-side client-streaming:
     *
     * @snippet client.cpp write_last-client-streaming-client-side
     *
     * Example client-side bidirectional-streaming:
     *
     * @snippet client.cpp write_last-bidirectional-client-side
     *
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` means that the data/metadata/status/etc is going to go to the wire.
     * If it is `false`, it is not going to the wire because the call is already dead (i.e., canceled, deadline expired,
     * other side dropped the channel, etc).
     */
    template <class Writer, class Message, class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(Writer& writer, const Message& message, grpc::WriteOptions options,
                    CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::WriteLastInitFunction<Message, detail::UnwrapUniquePtrT<Writer>>{detail::unwrap_unique_ptr(writer),
                                                                                     message, options},
            static_cast<CompletionToken&&>(token));
    }
};

/**
 * @brief Server-side function object to coalesce write and finish of streaming RPCs
 *
 * The examples below are based on the following .proto file:
 *
 * @snippet example.proto example-proto
 *
 * @attention The completion handler created from the completion token that is provided to the functions described below
 * must have an associated executor that refers to a GrpcContext:
 * @snippet server.cpp bind-executor-to-use-awaitable
 *
 * **Per-Operation Cancellation**
 *
 * None. Operations will be cancelled when the deadline of the RPC has been reached
 * (see
 * [grpc::ClientContext::set_deadline](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#ad4e16866fee3f6ee5a10efb5be6f4da6))
 * or the call has been cancelled
 * (see
 * [grpc::ClientContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#abd0f6715c30287b75288015eee628984)
 * and
 * [grpc::ServerContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_server_context.html#a88d3a0c3d53e39f38654ce8fba968301)).
 */
struct WriteAndFinishFn
{
    /**
     * @brief Coalesce write and finish of a streaming RPC
     *
     * Write response and coalesce it with trailing metadata which contains status, using WriteOptions
     * options.
     *
     * write_and_finish is equivalent of performing write_last and finish in a single step.
     *
     * gRPC does not take ownership or a reference to response and status, so it is safe to deallocate once
     * write_and_finish returns.
     *
     * Implicit input parameter:
     *
     * @arg The ServerContext associated with the call is used for sending trailing (and initial) metadata to the
     * client.
     *
     * @note Status must have an OK code.
     *
     * Example server-streaming:
     *
     * @snippet server.cpp write_and_finish-server-streaming-server-side
     *
     * Example bidirectional-streaming:
     *
     * @snippet server.cpp write_and_finish-bidirectional-streaming-server-side
     *
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` means that the data/metadata/status/etc is going to go to the wire.
     * If it is `false`, it is not going to the wire because the call is already dead (i.e., canceled, deadline expired,
     * other side dropped the channel, etc).
     */
    template <class Writer, class Response, class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(Writer& writer, const Response& response, grpc::WriteOptions options, const grpc::Status& status,
                    CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::ServerWriteAndFinishInitFunction<Response, detail::UnwrapUniquePtrT<Writer>>{
                detail::unwrap_unique_ptr(writer), response, options, status},
            static_cast<CompletionToken&&>(token));
    }
};

/**
 * @brief Server-side function object to finish RPCs with an error
 *
 * The examples below are based on the following .proto file:
 *
 * @snippet example.proto example-proto
 *
 * @attention The completion handler created from the completion token that is provided to the functions described below
 * must have an associated executor that refers to a GrpcContext:
 * @snippet server.cpp bind-executor-to-use-awaitable
 *
 * **Per-Operation Cancellation**
 *
 * None. Operations will be cancelled when the deadline of the RPC has been reached
 * (see
 * [grpc::ClientContext::set_deadline](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#ad4e16866fee3f6ee5a10efb5be6f4da6))
 * or the call has been cancelled
 * (see
 * [grpc::ClientContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#abd0f6715c30287b75288015eee628984)
 * and
 * [grpc::ServerContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_server_context.html#a88d3a0c3d53e39f38654ce8fba968301)).
 */
struct FinishWithErrorFn
{
    /**
     * @brief Finish an RPC with an error
     *
     * Indicate that the stream is to be finished with a non-OK status, and request notification for when the server has
     * finished sending the appropriate signals to the client to end the call.
     *
     * It should not be called concurrently with other streaming APIs on the same stream.
     *
     * Side effect:
     *
     * @arg Sends initial metadata if not already sent (using the ServerContext associated with this call).
     *
     * gRPC does not take ownership or a reference to status, so it is safe to deallocate once finish_with_error
     * returns.
     *
     * @note Status must have a non-OK code.
     *
     * Example client-streaming:
     *
     * @snippet server.cpp finish_with_error-client-streaming-server-side
     *
     * Example unary:
     *
     * @snippet server.cpp finish_with_error-unary-server-side
     *
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. The bool should always be `true`.
     */
    template <class Responder, class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(Responder& responder, const grpc::Status& status, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::ServerFinishWithErrorInitFunction<detail::UnwrapUniquePtrT<Responder>>{
                detail::unwrap_unique_ptr(responder), status},
            static_cast<CompletionToken&&>(token));
    }
};

/**
 * @brief Server-side function object to send initial metadata for RPCs
 *
 * The examples below are based on the following .proto file:
 *
 * @snippet example.proto example-proto
 *
 * @attention The completion handler created from the completion token that is provided to the functions described below
 * must have an associated executor that refers to a GrpcContext:
 * @snippet server.cpp bind-executor-to-use-awaitable
 *
 * **Per-Operation Cancellation**
 *
 * None. Operations will be cancelled when the deadline of the RPC has been reached
 * (see
 * [grpc::ClientContext::set_deadline](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#ad4e16866fee3f6ee5a10efb5be6f4da6))
 * or the call has been cancelled
 * (see
 * [grpc::ClientContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#abd0f6715c30287b75288015eee628984)
 * and
 * [grpc::ServerContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_server_context.html#a88d3a0c3d53e39f38654ce8fba968301)).
 */
struct SendInitialMetadataFn
{
    /**
     * @brief Send initial metadata
     *
     * Request notification of the sending of initial metadata to the client.
     *
     * This call is optional, but if it is used, it cannot be used concurrently with or after the Finish method.
     *
     * Example:
     *
     * @snippet server.cpp send_initial_metadata-unary-server-side
     *
     * @param responder `grpc::ServerAsyncResponseWriter`, `grpc::ServerAsyncReader`, `grpc::ServerAsyncWriter` or
     * `grpc::ServerAsyncReaderWriter`
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` means that the data/metadata/status/etc is going to go to the wire.
     * If it is `false`, it is not going to the wire because the call is already dead (i.e., canceled, deadline expired,
     * other side dropped the channel, etc).
     */
    template <class Responder, class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(Responder& responder, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::SendInitialMetadataInitFunction<detail::UnwrapUniquePtrT<Responder>>{
                detail::unwrap_unique_ptr(responder)},
            static_cast<CompletionToken&&>(token));
    }
};

/**
 * @brief Client-side function object to read initial metadata for RPCs
 *
 * The examples below are based on the following .proto file:
 *
 * @snippet example.proto example-proto
 *
 * @attention The completion handler created from the completion token that is provided to the functions described below
 * must have an associated executor that refers to a GrpcContext:
 * @snippet server.cpp bind-executor-to-use-awaitable
 *
 * **Per-Operation Cancellation**
 *
 * None. Operations will be cancelled when the deadline of the RPC has been reached
 * (see
 * [grpc::ClientContext::set_deadline](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#ad4e16866fee3f6ee5a10efb5be6f4da6))
 * or the call has been cancelled
 * (see
 * [grpc::ClientContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_context.html#abd0f6715c30287b75288015eee628984)
 * and
 * [grpc::ServerContext::TryCancel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_server_context.html#a88d3a0c3d53e39f38654ce8fba968301)).
 */
struct ReadInitialMetadataFn
{
    /**
     * @brief Read initial metadata
     *
     * Request notification of the reading of the initial metadata.
     *
     * This call is optional, but if it is used, it cannot be used concurrently with or after the read method.
     *
     * Side effect:
     *
     * @arg Upon receiving initial metadata from the server, the ClientContext associated with this call is updated, and
     * the calling code can access the received metadata through the ClientContext.
     *
     * Example:
     *
     * @snippet client.cpp read_initial_metadata-unary-client-side
     *
     * @attention For client-streaming and bidirectional-streaming RPCs: If the server does not explicitly send initial
     * metadata (e.g. by calling `agrpc::send_initial_metadata`) but waits for a message from the client instead then
     * this function won't complete until `agrpc::write` is called.
     *
     * @param responder `grpc::ClientAsyncResponseReader`, `grpc::ClientAsyncReader`, `grpc::ClientAsyncWriter` or
     * `grpc::ClientAsyncReaderWriter` (or a unique_ptr of them or their -Interface variants).
     * @param token A completion token like `asio::yield_context` or the one created by `agrpc::use_sender`. The
     * completion signature is `void(bool)`. `true` indicates that the metadata was read, `false` when the call is
     * dead.
     */
    template <class Responder, class CompletionToken = agrpc::DefaultCompletionToken>
    auto operator()(Responder& responder, CompletionToken&& token = {}) const
        noexcept(detail::IS_NOTRHOW_GRPC_INITIATE_COMPLETION_TOKEN<CompletionToken>)
    {
        return detail::grpc_initiate(
            detail::ReadInitialMetadataInitFunction<detail::UnwrapUniquePtrT<Responder>>{
                detail::unwrap_unique_ptr(responder)},
            static_cast<CompletionToken&&>(token));
    }
};
}  // namespace detail

/**
 * @brief Start a new RPC
 *
 * @link detail::RequestFn
 * Client and server-side function to start RPCs.
 * @endlink
 */
inline constexpr detail::RequestFn request{};

/**
 * @brief Read from a streaming RPC
 *
 * @link detail::ReadFn
 * Client and server-side function to read from streaming RPCs.
 * @endlink
 */
inline constexpr detail::ReadFn read{};

/**
 * @brief Write to a streaming RPC
 *
 * @link detail::WriteFn
 * Client and server-side function to write to streaming RPCs.
 * @endlink
 */
inline constexpr detail::WriteFn write{};

/**
 * @brief Signal WritesDone to a streaming RPC
 *
 * @link detail::WritesDoneFn
 * Client-side function to signal WritesDone to streaming RPCs.
 * @endlink
 */
inline constexpr detail::WritesDoneFn writes_done{};

/**
 * @brief Finish a RPC
 *
 * @link detail::FinishFn
 * Client and server-side function to finish RPCs.
 * @endlink
 */
inline constexpr detail::FinishFn finish{};

/**
 * @brief Coalesce write and send trailing metadata of a streaming RPC
 *
 * @link detail::WriteLastFn
 * Client and server-side function to coalesce write and send trailing metadata of streaming RPCs.
 * @endlink
 */
inline constexpr detail::WriteLastFn write_last{};

/**
 * @brief Coalesce write and finish of a streaming RPC
 *
 * @link detail::WriteAndFinishFn
 * Server-side function to coalesce write and finish of streaming RPCs.
 * @endlink
 */
inline constexpr detail::WriteAndFinishFn write_and_finish{};

/**
 * @brief Finish a RPC with an error
 *
 * @link detail::FinishWithErrorFn
 * Server-side function to finish RPCs with an error.
 * @endlink
 */
inline constexpr detail::FinishWithErrorFn finish_with_error{};

/**
 * @brief Send initial metadata for a RPC
 *
 * @link detail::SendInitialMetadataFn
 * Server-side function to send initial metadata for RPCs.
 * @endlink
 */
inline constexpr detail::SendInitialMetadataFn send_initial_metadata{};

/**
 * @brief Read initial metadata for a RPC
 *
 * @link detail::ReadInitialMetadataFn
 * Client-side function to read initial metadata for RPCs.
 * @endlink
 */
inline constexpr detail::ReadInitialMetadataFn read_initial_metadata{};

AGRPC_NAMESPACE_END

#endif  // AGRPC_AGRPC_RPC_HPP
