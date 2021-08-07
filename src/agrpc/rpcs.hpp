#ifndef AGRPC_AGRPC_RPCS_HPP
#define AGRPC_AGRPC_RPCS_HPP

#include "agrpc/detail/asioForward.hpp"
#include "agrpc/detail/initiate.hpp"
#include "agrpc/detail/rpcs.hpp"
#include "agrpc/initiate.hpp"

#include <grpcpp/alarm.h>

namespace agrpc
{
template <class Deadline, class CompletionToken = agrpc::DefaultCompletionToken>
auto wait(grpc::Alarm& alarm, const Deadline& deadline, CompletionToken token = {})
{
    return grpc_initiate(
        [&, deadline](agrpc::GrpcContext& grpc_context, void* tag)
        {
            alarm.Set(grpc_context.get_completion_queue(), deadline, tag);
        },
        token);
}

/*
Server
*/
template <class RPC, class Service, class Request, class Responder,
          class CompletionToken = agrpc::DefaultCompletionToken>
auto request(detail::ServerMultiArgRequest<RPC, Request, Responder> rpc, Service& service,
             grpc::ServerContext& server_context, Request& request, Responder& responder, CompletionToken token = {})
{
    return grpc_initiate(
        [&, rpc](agrpc::GrpcContext& grpc_context, void* tag)
        {
            auto* cq = grpc_context.get_server_completion_queue();
            (service.*rpc)(&server_context, &request, &responder, cq, cq, tag);
        },
        token);
}

template <class RPC, class Service, class Responder, class CompletionToken = agrpc::DefaultCompletionToken>
auto request(detail::ServerSingleArgRequest<RPC, Responder> rpc, Service& service, grpc::ServerContext& server_context,
             Responder& responder, CompletionToken token = {})
{
    return grpc_initiate(
        [&, rpc](agrpc::GrpcContext& grpc_context, void* tag)
        {
            auto* cq = grpc_context.get_server_completion_queue();
            (service.*rpc)(&server_context, &responder, cq, cq, tag);
        },
        token);
}

template <class Response, class Request, class CompletionToken = agrpc::DefaultCompletionToken>
auto read(grpc::ServerAsyncReader<Response, Request>& reader, Request& request, CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            reader.Read(&request, tag);
        },
        token);
}

template <class Response, class Request, class CompletionToken = agrpc::DefaultCompletionToken>
auto read(grpc::ServerAsyncReaderWriter<Response, Request>& reader_writer, Request& request, CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            reader_writer.Read(&request, tag);
        },
        token);
}

template <class Response, class CompletionToken = agrpc::DefaultCompletionToken>
auto write(grpc::ServerAsyncWriter<Response>& writer, const Response& response, CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            writer.Write(response, tag);
        },
        token);
}

template <class Response, class Request, class CompletionToken = agrpc::DefaultCompletionToken>
auto write(grpc::ServerAsyncReaderWriter<Response, Request>& reader_writer, const Response& response,
           CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            reader_writer.Write(response, tag);
        },
        token);
}

template <class Response, class CompletionToken = agrpc::DefaultCompletionToken>
auto finish(grpc::ServerAsyncWriter<Response>& writer, const grpc::Status& status, CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            writer.Finish(status, tag);
        },
        token);
}

template <class Response, class Request, class CompletionToken = agrpc::DefaultCompletionToken>
auto finish(grpc::ServerAsyncReader<Response, Request>& reader, const Response& response, const grpc::Status& status,
            CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            reader.Finish(response, status, tag);
        },
        token);
}

template <class Response, class CompletionToken = agrpc::DefaultCompletionToken>
auto finish(grpc::ServerAsyncResponseWriter<Response>& writer, const Response& response, const grpc::Status& status,
            CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            writer.Finish(response, status, tag);
        },
        token);
}

template <class Response, class Request, class CompletionToken = agrpc::DefaultCompletionToken>
auto finish(grpc::ServerAsyncReaderWriter<Response, Request>& reader_writer, const grpc::Status& status,
            CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            reader_writer.Finish(status, tag);
        },
        token);
}

template <class Response, class Request, class CompletionToken = agrpc::DefaultCompletionToken>
auto write_and_finish(grpc::ServerAsyncReaderWriter<Response, Request>& reader_writer, const Response& response,
                      grpc::WriteOptions options, const grpc::Status& status, CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            reader_writer.WriteAndFinish(response, options, status, tag);
        },
        token);
}

template <class Response, class Request, class CompletionToken = agrpc::DefaultCompletionToken>
auto finish_with_error(grpc::ServerAsyncReader<Response, Request>& reader, const grpc::Status& status,
                       CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            reader.FinishWithError(status, tag);
        },
        token);
}

template <class Response, class CompletionToken = agrpc::DefaultCompletionToken>
auto finish_with_error(grpc::ServerAsyncResponseWriter<Response>& writer, const grpc::Status& status,
                       CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            writer.FinishWithError(status, tag);
        },
        token);
}

/*
Client
*/
#ifdef __cpp_impl_coroutine
template <class RPC, class Stub, class Request, class Reader, class Executor = asio::any_io_executor>
auto request(detail::ClientUnaryRequest<RPC, Request, Reader>, Stub& stub, grpc::ClientContext& client_context,
             const Request& request, asio::use_awaitable_t<Executor> = {})
    -> asio::async_result<asio::use_awaitable_t<Executor>, void(Reader)>::return_type
{
    const auto executor = co_await asio::this_coro::executor;
    co_return stub.AsyncUnary(&client_context, request,
                              static_cast<agrpc::GrpcContext&>(executor.context()).get_completion_queue());
}
#endif

template <class RPC, class Stub, class Request, class Reader,
          class CompletionToken
#ifndef __cpp_impl_coroutine
          = agrpc::DefaultCompletionToken
#endif
          >
auto request(detail::ClientUnaryRequest<RPC, Request, Reader> rpc, Stub& stub, grpc::ClientContext& client_context,
             const Request& request, CompletionToken token = {})
{
    const auto executor = asio::get_associated_executor(token);
    return stub.AsyncUnary(&client_context, request,
                           static_cast<agrpc::GrpcContext&>(executor.context()).get_completion_queue());
}

#ifdef __cpp_impl_coroutine
template <class RPC, class Stub, class Request, class Reader, class Executor = asio::any_io_executor>
auto request(detail::ClientUnaryRequest<RPC, Request, Reader>, Stub& stub, grpc::ClientContext& client_context,
             const Request& request, Reader& reader, asio::use_awaitable_t<Executor> = {})
    -> asio::async_result<asio::use_awaitable_t<Executor>, void()>::return_type
{
    const auto executor = co_await asio::this_coro::executor;
    reader = stub.AsyncUnary(&client_context, request,
                             static_cast<agrpc::GrpcContext&>(executor.context()).get_completion_queue());
}
#endif

template <class RPC, class Stub, class Request, class Reader,
          class CompletionToken
#ifndef __cpp_impl_coroutine
          = agrpc::DefaultCompletionToken
#endif
          >
void request(detail::ClientUnaryRequest<RPC, Request, Reader> rpc, Stub& stub, grpc::ClientContext& client_context,
             const Request& request, Reader& reader, CompletionToken token = {})
{
    const auto executor = asio::get_associated_executor(token);
    reader = stub.AsyncUnary(&client_context, request,
                             static_cast<agrpc::GrpcContext&>(executor.context()).get_completion_queue());
}

template <class RPC, class Stub, class Request, class Reader, class CompletionToken = agrpc::DefaultCompletionToken>
auto request(detail::ClientServerStreamingRequest<RPC, Request, Reader> rpc, Stub& stub,
             grpc::ClientContext& client_context, const Request& request, CompletionToken token = {})
{
    return asio::async_initiate<CompletionToken, void(std::pair<Reader, bool>)>(
        [&, rpc](auto completion_handler) mutable
        {
            detail::create_work_and_invoke(
                detail::make_completion_handler_with_responder<Reader>(std::move(completion_handler)),
                [&](auto&& grpc_context, auto* tag)
                {
                    tag->handler().responder =
                        (stub.*rpc)(&client_context, request, grpc_context.get_completion_queue(), tag);
                });
        },
        token);
}

template <class RPC, class Stub, class Request, class Reader, class CompletionToken = agrpc::DefaultCompletionToken>
auto request(detail::ClientServerStreamingRequest<RPC, Request, Reader> rpc, Stub& stub,
             grpc::ClientContext& client_context, const Request& request, Reader& reader, CompletionToken token = {})
{
    return grpc_initiate(
        [&, rpc](agrpc::GrpcContext& grpc_context, void* tag) mutable
        {
            reader = (stub.*rpc)(&client_context, request, grpc_context.get_completion_queue(), tag);
        },
        token);
}

template <class RPC, class Stub, class Writer, class Response, class CompletionToken = agrpc::DefaultCompletionToken>
auto request(detail::ClientSideStreamingRequest<RPC, Writer, Response> rpc, Stub& stub,
             grpc::ClientContext& client_context, Response& response, CompletionToken token = {})
{
    return asio::async_initiate<CompletionToken, void(std::pair<Writer, bool>)>(
        [&, rpc](auto completion_handler) mutable
        {
            detail::create_work_and_invoke(
                detail::make_completion_handler_with_responder<Writer>(std::move(completion_handler)),
                [&](auto&& grpc_context, auto* tag)
                {
                    tag->handler().responder =
                        (stub.*rpc)(&client_context, &response, grpc_context.get_completion_queue(), tag);
                });
        },
        token);
}

template <class RPC, class Stub, class Writer, class Response, class CompletionToken = agrpc::DefaultCompletionToken>
auto request(detail::ClientSideStreamingRequest<RPC, Writer, Response> rpc, Stub& stub,
             grpc::ClientContext& client_context, Writer& writer, Response& response, CompletionToken token = {})
{
    return grpc_initiate(
        [&, rpc](agrpc::GrpcContext& grpc_context, void* tag) mutable
        {
            writer = (stub.*rpc)(&client_context, &response, grpc_context.get_completion_queue(), tag);
        },
        token);
}

template <class RPC, class Stub, class ReaderWriter, class CompletionToken = agrpc::DefaultCompletionToken>
auto request(detail::ClientBidirectionalStreamingRequest<RPC, ReaderWriter> rpc, Stub& stub,
             grpc::ClientContext& client_context, CompletionToken token = {})
{
    return asio::async_initiate<CompletionToken, void(std::pair<ReaderWriter, bool>)>(
        [&, rpc](auto completion_handler) mutable
        {
            detail::create_work_and_invoke(
                detail::make_completion_handler_with_responder<ReaderWriter>(std::move(completion_handler)),
                [&](auto&& grpc_context, auto* tag)
                {
                    tag->handler().responder = (stub.*rpc)(&client_context, grpc_context.get_completion_queue(), tag);
                });
        },
        token);
}

template <class RPC, class Stub, class ReaderWriter, class CompletionToken = agrpc::DefaultCompletionToken>
auto request(detail::ClientBidirectionalStreamingRequest<RPC, ReaderWriter> rpc, Stub& stub,
             grpc::ClientContext& client_context, ReaderWriter& reader_writer, CompletionToken token = {})
{
    return grpc_initiate(
        [&, rpc](agrpc::GrpcContext& grpc_context, void* tag) mutable
        {
            reader_writer = (stub.*rpc)(&client_context, grpc_context.get_completion_queue(), tag);
        },
        token);
}

template <class Response, class CompletionToken = agrpc::DefaultCompletionToken>
auto read(grpc::ClientAsyncReader<Response>& reader, Response& response, CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            reader.Read(&response, tag);
        },
        token);
}

template <class Request, class Response, class CompletionToken = agrpc::DefaultCompletionToken>
auto read(grpc::ClientAsyncReaderWriter<Request, Response>& reader_writer, Response& response,
          CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            reader_writer.Read(&response, tag);
        },
        token);
}

template <class Request, class CompletionToken = agrpc::DefaultCompletionToken>
auto write(grpc::ClientAsyncWriter<Request>& writer, const Request& request, CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            writer.Write(request, tag);
        },
        token);
}

template <class Request, class Response, class CompletionToken = agrpc::DefaultCompletionToken>
auto write(grpc::ClientAsyncReaderWriter<Request, Response>& reader_writer, const Request& request,
           CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            reader_writer.Write(request, tag);
        },
        token);
}

template <class Response, class CompletionToken = agrpc::DefaultCompletionToken>
auto finish(grpc::ClientAsyncReader<Response>& reader, grpc::Status& status, CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            reader.Finish(&status, tag);
        },
        token);
}

template <class Request, class CompletionToken = agrpc::DefaultCompletionToken>
auto finish(grpc::ClientAsyncWriter<Request>& writer, grpc::Status& status, CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            writer.Finish(&status, tag);
        },
        token);
}

template <class Response, class CompletionToken = agrpc::DefaultCompletionToken>
auto finish(grpc::ClientAsyncResponseReader<Response>& reader, Response& response, grpc::Status& status,
            CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            reader.Finish(&response, &status, tag);
        },
        token);
}

template <class Request, class Response, class CompletionToken = agrpc::DefaultCompletionToken>
auto finish(grpc::ClientAsyncReaderWriter<Request, Response>& reader_writer, grpc::Status& status,
            CompletionToken token = {})
{
    return grpc_initiate(
        [&](agrpc::GrpcContext&, void* tag)
        {
            reader_writer.Finish(&status, tag);
        },
        token);
}
}  // namespace agrpc

#endif  // AGRPC_AGRPC_RPCS_HPP
