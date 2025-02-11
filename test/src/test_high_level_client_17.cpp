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

#include "test/v1/test.grpc.pb.h"
#include "utils/asio_utils.hpp"
#include "utils/delete_guard.hpp"
#include "utils/doctest.hpp"
#include "utils/exception.hpp"
#include "utils/high_level_client.hpp"
#include "utils/inline_executor.hpp"
#include "utils/io_context_test.hpp"
#include "utils/protobuf.hpp"
#include "utils/rpc.hpp"
#include "utils/time.hpp"

#include <agrpc/high_level_client.hpp>
#include <agrpc/notify_when_done.hpp>
#include <agrpc/wait.hpp>
#include <grpcpp/grpcpp.h>

#include <future>

template <class RPC>
struct HighLevelClientIoContextTest : test::HighLevelClientTest<RPC>, test::IoContextTest
{
    void run_server_client_on_separate_threads(std::function<void(const asio::yield_context&)> server_func,
                                               std::function<void(const asio::yield_context&)> client_func)
    {
        test::typed_spawn(io_context,
                          [client_func, g = this->get_work_tracking_executor()](const asio::yield_context& yield)
                          {
                              client_func(yield);
                          });
        this->run_io_context_detached(false);
        this->spawn_and_run(
            [server_func](const asio::yield_context& yield)
            {
                server_func(yield);
            });
    }
};

template <class RPC>
auto get_status_code(const RPC& rpc)
{
    return rpc.status_code();
}

auto get_status_code(const grpc::Status& status) { return status.error_code(); }

TEST_CASE_TEMPLATE("Streaming RPC::request automatically finishes RPC on error", RPC, test::UnaryRPC,
                   test::UnaryInterfaceRPC, test::GenericUnaryRPC, test::ClientStreamingRPC,
                   test::ClientStreamingInterfaceRPC, test::ServerStreamingRPC, test::ServerStreamingInterfaceRPC,
                   test::BidirectionalStreamingRPC, test::BidirectionalStreamingInterfaceRPC)
{
    test::HighLevelClientTest<RPC> test;
    bool use_executor_overload{};
    SUBCASE("executor overload") {}
    SUBCASE("GrpcContext overload") { use_executor_overload = true; }
    test.server->Shutdown();
    test.client_context.set_deadline(test::ten_milliseconds_from_now());
    test.request_rpc(use_executor_overload,
                     [](auto&& rpc)
                     {
                         grpc::StatusCode status_code = get_status_code(rpc);
                         CHECK_MESSAGE((grpc::StatusCode::DEADLINE_EXCEEDED == status_code ||
                                        grpc::StatusCode::UNAVAILABLE == status_code),
                                       status_code);
                     });
    test.grpc_context.run();
}

TEST_CASE_FIXTURE(test::HighLevelClientTest<test::ServerStreamingRPC>,
                  "UnaryRPC::request exception thrown from completion handler rethrows from GrpcContext.run()")
{
    CHECK_THROWS_AS(spawn_and_run(
                        [&](const asio::yield_context& yield)
                        {
                            test_server.request_rpc(yield);
                            agrpc::finish(test_server.responder, grpc::Status::OK, yield);
                        },
                        [&](const asio::yield_context& yield)
                        {
                            auto rpc = std::make_unique<test::ServerStreamingRPC>(request_rpc(yield));
                            auto& rpc_ref = *rpc;
                            rpc_ref.read(response, asio::bind_executor(test::InlineExecutor{},
                                                                       [&, rpc = std::move(rpc)](bool)
                                                                       {
                                                                           throw test::Exception{};
                                                                       }));
                        }),
                    test::Exception);
}

TEST_CASE_TEMPLATE("RPC::read_initial_metadata successfully", RPC, test::ClientStreamingRPC, test::ServerStreamingRPC,
                   test::BidirectionalStreamingRPC)
{
    test::HighLevelClientTest<RPC> test;
    test.spawn_and_run(
        [&](const asio::yield_context& yield)
        {
            test.test_server.request_rpc(yield);
            agrpc::send_initial_metadata(test.test_server.responder, yield);
        },
        [&](const asio::yield_context& yield)
        {
            auto rpc = test.request_rpc(yield);
            CHECK(rpc.read_initial_metadata(yield));
        });
}

TEST_CASE_TEMPLATE("RPC::read_initial_metadata automatically finishes RPC on error", RPC, test::ClientStreamingRPC,
                   test::ServerStreamingRPC)
{
    test::HighLevelClientTest<RPC> test;
    test.spawn_and_run(
        [&](const asio::yield_context& yield)
        {
            test.server_request_rpc_and_cancel(yield);
        },
        [&](const asio::yield_context& yield)
        {
            auto rpc = test.request_rpc(yield);
            CHECK(rpc.ok());
            test.client_context.TryCancel();
            CHECK_FALSE(rpc.read_initial_metadata(yield));
            CHECK_EQ(grpc::StatusCode::CANCELLED, rpc.status_code());
            test.server_shutdown.initiate();
        });
}

#ifdef AGRPC_ASIO_HAS_SENDER_RECEIVER
TEST_CASE_FIXTURE(test::HighLevelClientTest<test::UnaryRPC>,
                  "RPC::request can have UseSender as default completion token")
{
    using RPC = agrpc::UseSender::as_default_on_t<agrpc::RPC<&test::v1::Test::Stub::PrepareAsyncUnary>>;
    bool ok{};
    test::DeleteGuard guard{};
    bool use_submit{};
    SUBCASE("submit") { use_submit = true; }
    SUBCASE("start") {}
    spawn_and_run(
        [&](const asio::yield_context& yield)
        {
            CHECK(test_server.request_rpc(yield));
            CHECK_EQ(42, test_server.request.integer());
            test_server.response.set_integer(21);
            CHECK(agrpc::finish(test_server.responder, test_server.response, grpc::Status::OK, yield));
        },
        [&](auto&&)
        {
            request.set_integer(42);
            auto sender = RPC::request(grpc_context, *stub, client_context, request, response);
            const test::FunctionAsReceiver receiver{[&](grpc::Status&& status)
                                                    {
                                                        ok = status.ok();
                                                    }};
            if (use_submit)
            {
                CHECK_FALSE(asio::execution::can_submit_v<std::add_const_t<decltype(sender)>, decltype(receiver)>);
                asio::execution::submit(std::move(sender), receiver);
            }
            else
            {
                CHECK_FALSE(asio::execution::can_connect_v<std::add_const_t<decltype(sender)>, decltype(receiver)>);
                auto& operation_state = guard.emplace_with(
                    [&]
                    {
                        return asio::execution::connect(std::move(sender), receiver);
                    });
                asio::execution::start(operation_state);
            }
        });
    CHECK(ok);
    CHECK_EQ(21, response.integer());
}
#endif

TEST_CASE_FIXTURE(test::HighLevelClientTest<test::GenericUnaryRPC>, "RPC::request generic unary RPC successfully")
{
    bool use_executor_overload{};
    SUBCASE("executor overload") {}
    SUBCASE("GrpcContext overload") { use_executor_overload = true; }
    spawn_and_run(
        [&](const asio::yield_context& yield)
        {
            CHECK(test_server.request_rpc(yield));
            CHECK_EQ(42, test_server.request.integer());
            test_server.response.set_integer(24);
            CHECK(agrpc::finish(test_server.responder, test_server.response, grpc::Status::OK, yield));
        },
        [&](const asio::yield_context& yield)
        {
            test::msg::Request typed_request;
            typed_request.set_integer(42);
            request = test::message_to_grpc_buffer(typed_request);
            auto status = request_rpc(use_executor_overload, yield);
            CHECK(status.ok());
            CHECK_EQ(24, test::grpc_buffer_to_message<test::msg::Response>(response).integer());
        });
}

TEST_CASE_FIXTURE(test::HighLevelClientTest<test::ServerStreamingRPC>, "ServerStreamingRPC::read successfully")
{
    bool use_executor_overload{};
    SUBCASE("executor overload") {}
    SUBCASE("GrpcContext overload") { use_executor_overload = true; }
    spawn_and_run(
        [&](const asio::yield_context& yield)
        {
            CHECK(test_server.request_rpc(yield));
            CHECK_EQ(42, test_server.request.integer());
            test_server.response.set_integer(1);
            CHECK(agrpc::write(test_server.responder, test_server.response, yield));
            CHECK(agrpc::finish(test_server.responder, grpc::Status::OK, yield));
        },
        [&](const asio::yield_context& yield)
        {
            request.set_integer(42);
            auto rpc = request_rpc(use_executor_overload, yield);
            CHECK(rpc.read(response, yield));
            CHECK_EQ(1, response.integer());
            CHECK_FALSE(rpc.read(response, yield));
            CHECK_EQ(grpc::StatusCode::OK, rpc.status_code());
        });
}

TEST_CASE_FIXTURE(test::HighLevelClientTest<test::ServerStreamingRPC>,
                  "ServerStreamingRPC::read automatically finishes on error")
{
    spawn_and_run(
        [&](const asio::yield_context& yield)
        {
            server_request_rpc_and_cancel(yield);
        },
        [&](const asio::yield_context& yield)
        {
            auto rpc = test::ServerStreamingRPC::request(grpc_context, *stub, client_context, request, yield);
            client_context.TryCancel();
            CHECK_FALSE(rpc.read(response, yield));
            CHECK_EQ(grpc::StatusCode::CANCELLED, rpc.status_code());
            server_shutdown.initiate();
        });
}

TEST_CASE_FIXTURE(test::HighLevelClientTest<test::ServerStreamingRPC>,
                  "ServerStreamingRPC can handle ClientContext.TryCancel")
{
    bool explicit_try_cancel{};
    SUBCASE("automatic TryCancel on destruction") {}
    SUBCASE("explicit TryCancel") { explicit_try_cancel = true; }
    spawn_and_run(
        [&](const asio::yield_context& yield)
        {
            server_request_rpc_and_cancel(yield);
        },
        [&](const asio::yield_context& yield)
        {
            {
                auto rpc = test::ServerStreamingRPC::request(grpc_context, *stub, client_context, request, yield);
                if (explicit_try_cancel)
                {
                    client_context.TryCancel();
                }
            }
            server_shutdown.initiate();
        });
}

auto create_is_cancelled_future(agrpc::GrpcContext& grpc_context, grpc::ServerContext& server_context)
{
    std::promise<bool> is_cancelled_promise;
    auto future = is_cancelled_promise.get_future();
    agrpc::notify_when_done(grpc_context, server_context,
                            [&, promise = std::move(is_cancelled_promise)]() mutable
                            {
                                promise.set_value(server_context.IsCancelled());
                            });
    return future;
}

TEST_CASE_FIXTURE(HighLevelClientIoContextTest<test::ClientStreamingRPC>,
                  "ClientStreamingRPC assigning to an active RPC cancels it")
{
    run_server_client_on_separate_threads(
        [&](const asio::yield_context& yield)
        {
            auto is_cancelled_future = create_is_cancelled_future(grpc_context, server_context);
            CHECK(test_server.request_rpc(yield));
            agrpc::read(test_server.responder, test_server.request, yield);

            // start and finish second request
            grpc::ServerContext new_server_context;
            grpc::ServerAsyncReader<test::msg::Response, test::msg::Request> responder{&new_server_context};
            CHECK(agrpc::request(&test::v1::Test::AsyncService::RequestClientStreaming, test_server.service,
                                 new_server_context, responder, yield));
            CHECK(agrpc::finish(responder, test_server.response, grpc::Status::OK, yield));

            // wait for cancellation signal from first request
            grpc::Alarm alarm;
            for (int i{}; i < 50; ++i)
            {
                agrpc::wait(alarm, test::ten_milliseconds_from_now(), yield);
                if (is_cancelled_future.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
                {
                    CHECK(is_cancelled_future.get());
                    return;
                }
            }
            FAIL("timeout reached while waiting for cancellation signal");
        },
        [&](const asio::yield_context& yield)
        {
            grpc::ClientContext new_client_context;
            new_client_context.set_deadline(test::five_seconds_from_now());
            test::ClientStreamingRPC rpc;
            rpc = request_rpc(yield);
            rpc.write(request, yield);
            rpc = test::ClientStreamingRPC::request(grpc_context, *stub, new_client_context, response, yield);
            CHECK(rpc.ok());
            CHECK(rpc.finish(yield));
        });
}

TEST_CASE_FIXTURE(test::HighLevelClientTest<test::ClientStreamingRPC>, "ClientStreamingRPC::write successfully")
{
    bool use_executor_overload{};
    SUBCASE("executor overload") {}
    SUBCASE("GrpcContext overload") { use_executor_overload = true; }
    bool set_last_message{};
    SUBCASE("write and finish separately") {}
    SUBCASE("set_last_message") { set_last_message = true; }
    spawn_and_run(
        [&](const asio::yield_context& yield)
        {
            CHECK(test_server.request_rpc(yield));
            CHECK(agrpc::read(test_server.responder, test_server.request, yield));
            CHECK_EQ(42, test_server.request.integer());
            test_server.response.set_integer(1);
            CHECK_FALSE(agrpc::read(test_server.responder, test_server.request, yield));
            CHECK(agrpc::finish(test_server.responder, test_server.response, grpc::Status::OK, yield));
        },
        [&](const asio::yield_context& yield)
        {
            auto rpc = request_rpc(use_executor_overload, yield);
            request.set_integer(42);
            if (set_last_message)
            {
                grpc::WriteOptions options{};
                CHECK(rpc.write(request, options.set_last_message(), yield));
            }
            else
            {
                CHECK(rpc.write(request, yield));
                CHECK(rpc.finish(yield));
            }
            CHECK_EQ(grpc::StatusCode::OK, rpc.status_code());
        });
}

TEST_CASE_FIXTURE(test::HighLevelClientTest<test::ClientStreamingRPC>,
                  "ClientStreamingRPC::write automatically finishes on error")
{
    grpc::WriteOptions options{};
    SUBCASE("") {}
    SUBCASE("set_last_message") { options.set_last_message(); }
    spawn_and_run(
        [&](const asio::yield_context& yield)
        {
            server_request_rpc_and_cancel(yield);
        },
        [&](const asio::yield_context& yield)
        {
            auto rpc = test::ClientStreamingRPC::request(grpc_context, *stub, client_context, response, yield);
            client_context.TryCancel();
            CHECK_FALSE(rpc.write(request, options, yield));
            CHECK_EQ(grpc::StatusCode::CANCELLED, rpc.status_code());
            server_shutdown.initiate();
        });
}

TEST_CASE_FIXTURE(test::HighLevelClientTest<test::ClientStreamingRPC>,
                  "ClientStreamingRPC::finish can be called multiple times on a successful RPC")
{
    spawn_and_run(
        [&](const asio::yield_context& yield)
        {
            CHECK(test_server.request_rpc(yield));
            CHECK(agrpc::finish(test_server.responder, test_server.response, grpc::Status::OK, yield));
        },
        [&](const asio::yield_context& yield)
        {
            auto rpc = test::ClientStreamingRPC::request(grpc_context, *stub, client_context, response, yield);
            CHECK(rpc.finish(yield));
            CHECK_EQ(grpc::StatusCode::OK, rpc.status_code());
            CHECK(rpc.finish(yield));
            CHECK_EQ(grpc::StatusCode::OK, rpc.status_code());
        });
}

TEST_CASE_FIXTURE(test::HighLevelClientTest<test::ClientStreamingRPC>,
                  "ClientStreamingRPC::finish can be called after set_last_message")
{
    spawn_and_run(
        [&](const asio::yield_context& yield)
        {
            CHECK(test_server.request_rpc(yield));
            CHECK(agrpc::read(test_server.responder, test_server.request, yield));
            CHECK(agrpc::finish(test_server.responder, test_server.response, grpc::Status::OK, yield));
        },
        [&](const asio::yield_context& yield)
        {
            auto rpc = test::ClientStreamingRPC::request(grpc_context, *stub, client_context, response, yield);
            CHECK(rpc.write(request, grpc::WriteOptions{}.set_last_message(), yield));
            CHECK(rpc.finish(yield));
            CHECK_EQ(grpc::StatusCode::OK, rpc.status_code());
            CHECK(rpc.finish(yield));
            CHECK_EQ(grpc::StatusCode::OK, rpc.status_code());
        });
}

TEST_CASE_FIXTURE(test::HighLevelClientTest<test::ClientStreamingRPC>,
                  "ClientStreamingRPC::finish can be called multiple times on a failed RPC")
{
    spawn_and_run(
        [&](const asio::yield_context& yield)
        {
            server_request_rpc_and_cancel(yield);
        },
        [&](const asio::yield_context& yield)
        {
            auto rpc = test::ClientStreamingRPC::request(grpc_context, *stub, client_context, response, yield);
            client_context.TryCancel();
            CHECK_FALSE(rpc.finish(yield));
            CHECK_EQ(grpc::StatusCode::CANCELLED, rpc.status_code());
            CHECK_FALSE(rpc.finish(yield));
            CHECK_EQ(grpc::StatusCode::CANCELLED, rpc.status_code());
            server_shutdown.initiate();
        });
}

#ifdef AGRPC_ASIO_HAS_SENDER_RECEIVER
TEST_CASE_FIXTURE(test::HighLevelClientTest<test::ClientStreamingRPC>,
                  "ClientStreamingRPC::finish can be called multiple times using sender")
{
    bool expected_ok = true;
    auto expected_status_code = grpc::StatusCode::OK;
    SUBCASE("success") {}
    SUBCASE("failure")
    {
        expected_ok = false;
        expected_status_code = grpc::StatusCode::CANCELLED;
    }
    spawn_and_run(
        [&](const asio::yield_context& yield)
        {
            test_server.request_rpc(yield);
            if (expected_ok)
            {
                CHECK(agrpc::finish(test_server.responder, test_server.response, grpc::Status::OK, yield));
            }
            else
            {
                server_context.TryCancel();
            }
        },
        [&](asio::yield_context yield)
        {
            auto rpc = std::make_unique<test::ClientStreamingRPC>(
                test::ClientStreamingRPC::request(grpc_context, *stub, client_context, response, yield));
            if (!expected_ok)
            {
                client_context.TryCancel();
            }
            auto& rpc_ref = *rpc;
            asio::execution::submit(
                rpc_ref.finish(agrpc::use_sender),
                test::FunctionAsReceiver{[&, rpc = std::move(rpc)](bool ok) mutable
                                         {
                                             CHECK_EQ(expected_ok, ok);
                                             CHECK_EQ(expected_status_code, rpc->status_code());
                                             asio::execution::submit(
                                                 rpc_ref.finish(agrpc::use_sender),
                                                 test::FunctionAsReceiver{[&, rpc = std::move(rpc)](bool ok)
                                                                          {
                                                                              CHECK_EQ(expected_ok, ok);
                                                                              CHECK_EQ(expected_status_code,
                                                                                       rpc->status_code());
                                                                          }});
                                         }});
        });
}
#endif

TEST_CASE_FIXTURE(HighLevelClientIoContextTest<test::BidirectionalStreamingRPC>, "BidirectionalStreamingRPC success")
{
    bool use_executor_overload{};
    SUBCASE("executor overload") {}
    SUBCASE("GrpcContext overload") { use_executor_overload = true; }
    run_server_client_on_separate_threads(
        [&](const asio::yield_context& yield)
        {
            CHECK(test_server.request_rpc(yield));
            test_server.response.set_integer(1);
            CHECK(agrpc::read(test_server.responder, test_server.request, yield));
            CHECK_FALSE(agrpc::read(test_server.responder, test_server.request, yield));
            CHECK_EQ(42, test_server.request.integer());
            CHECK(agrpc::write(test_server.responder, test_server.response, yield));
            CHECK(agrpc::finish(test_server.responder, grpc::Status::OK, yield));
        },
        [&](const asio::yield_context& yield)
        {
            auto rpc = request_rpc(use_executor_overload, yield);
            request.set_integer(42);
            CHECK(rpc.write(request, yield));
            CHECK(rpc.writes_done(yield));
            CHECK(rpc.read(response, yield));
            CHECK_EQ(1, response.integer());
            CHECK(rpc.writes_done(yield));
            CHECK_FALSE(rpc.read(response, yield));
            CHECK_EQ(1, response.integer());
            CHECK(rpc.finish(yield));
            CHECK_EQ(grpc::StatusCode::OK, rpc.status_code());
            CHECK(rpc.finish(yield));
            CHECK_EQ(grpc::StatusCode::OK, rpc.status_code());
        });
}

TEST_CASE_FIXTURE(HighLevelClientIoContextTest<test::BidirectionalStreamingRPC>,
                  "BidirectionalStreamingRPC concurrent read+write")
{
    bool set_last_message{};
    SUBCASE("no WriteOptions") {}
    SUBCASE("set_last_message") { set_last_message = true; }
    run_server_client_on_separate_threads(
        [&](const asio::yield_context& yield)
        {
            CHECK(test_server.request_rpc(yield));
            CHECK(agrpc::write(test_server.responder, test_server.response, {}, yield));
            CHECK(agrpc::read(test_server.responder, test_server.request, yield));
            CHECK(agrpc::finish(test_server.responder, grpc::Status{grpc::StatusCode::ALREADY_EXISTS, ""}, yield));
        },
        [&](const asio::yield_context& yield)
        {
            auto rpc = test::BidirectionalStreamingRPC::request(grpc_context, *stub, client_context, yield);
            CHECK(rpc.read(response, yield));
            std::promise<bool> promise;
            const auto fulfill_promise = [&](bool ok)
            {
                promise.set_value(ok);
            };
            if (set_last_message)
            {
                grpc::WriteOptions options{};
                rpc.write(request, options.set_last_message(), fulfill_promise);
            }
            else
            {
                rpc.write(request, fulfill_promise);
            }
            CHECK_FALSE(rpc.read(response, yield));
            CHECK(promise.get_future().get());
            CHECK_FALSE(rpc.finish(yield));
            CHECK_EQ(grpc::StatusCode::ALREADY_EXISTS, rpc.status_code());
            CHECK_FALSE(rpc.finish(yield));
            CHECK_EQ(grpc::StatusCode::ALREADY_EXISTS, rpc.status_code());
        });
}

TEST_CASE_FIXTURE(HighLevelClientIoContextTest<test::BidirectionalStreamingRPC>,
                  "BidirectionalStreamingRPC TryCancel before write+read")
{
    run_server_client_on_separate_threads(
        [&](const asio::yield_context& yield)
        {
            CHECK(test_server.request_rpc(yield));
            agrpc::finish(test_server.responder, grpc::Status::OK, yield);
        },
        [&](const asio::yield_context& yield)
        {
            auto rpc = test::BidirectionalStreamingRPC::request(grpc_context, *stub, client_context, yield);
            client_context.TryCancel();
            std::promise<bool> promise;
            rpc.read(response,
                     [&](bool ok)
                     {
                         promise.set_value(ok);
                     });
            CHECK_FALSE(rpc.write(request, yield));
            CHECK_FALSE(promise.get_future().get());
            CHECK_FALSE(rpc.finish(yield));
            CHECK_EQ(grpc::StatusCode::CANCELLED, rpc.status_code());
        });
}
TEST_CASE_FIXTURE(HighLevelClientIoContextTest<test::GenericStreamingRPC>, "BidirectionalStreamingRPC generic success")
{
    run_server_client_on_separate_threads(
        [&](const asio::yield_context& yield)
        {
            CHECK(test_server.request_rpc(yield));
            test_server.response.set_integer(1);
            CHECK(agrpc::read(test_server.responder, test_server.request, yield));
            CHECK_FALSE(agrpc::read(test_server.responder, test_server.request, yield));
            CHECK_EQ(42, test_server.request.integer());
            CHECK(agrpc::write(test_server.responder, test_server.response, yield));
            CHECK(agrpc::finish(test_server.responder, grpc::Status::OK, yield));
        },
        [&](const asio::yield_context& yield)
        {
            auto rpc = request_rpc(yield);
            CHECK(rpc.ok());

            test::msg::Request typed_request;
            typed_request.set_integer(42);
            CHECK(rpc.write(test::message_to_grpc_buffer(typed_request), yield));
            CHECK(rpc.writes_done(yield));

            CHECK(rpc.read(response, yield));
            CHECK_EQ(1, test::grpc_buffer_to_message<test::msg::Response>(response).integer());

            CHECK(rpc.writes_done(yield));

            response.Clear();
            CHECK_FALSE(rpc.read(response, yield));

            CHECK(rpc.finish(yield));
            CHECK_EQ(grpc::StatusCode::OK, rpc.status_code());
            CHECK(rpc.finish(yield));
            CHECK_EQ(grpc::StatusCode::OK, rpc.status_code());
        });
}

TEST_CASE("RPC::service_name/method_name")
{
    const auto check_eq_and_null_terminated = [](std::string_view expected, std::string_view actual)
    {
        CHECK_EQ(expected, actual);
        CHECK_EQ('\0', *(actual.data() + actual.size()));
    };
    check_eq_and_null_terminated("test.v1.Test", test::UnaryRPC::service_name());
    check_eq_and_null_terminated("Unary", test::UnaryRPC::method_name());
    check_eq_and_null_terminated("test.v1.Test", test::ClientStreamingRPC::service_name());
    check_eq_and_null_terminated("ClientStreaming", test::ClientStreamingRPC::method_name());
    check_eq_and_null_terminated("test.v1.Test", test::ServerStreamingRPC::service_name());
    check_eq_and_null_terminated("ServerStreaming", test::ServerStreamingRPC::method_name());
    check_eq_and_null_terminated("test.v1.Test", test::BidirectionalStreamingRPC::service_name());
    check_eq_and_null_terminated("BidirectionalStreaming", test::BidirectionalStreamingRPC::method_name());
    check_eq_and_null_terminated("AsyncGenericService", test::GenericUnaryRPC::service_name());
    check_eq_and_null_terminated("", test::GenericUnaryRPC::method_name());
    check_eq_and_null_terminated("AsyncGenericService", test::GenericStreamingRPC::service_name());
    check_eq_and_null_terminated("", test::GenericStreamingRPC::method_name());
}

#ifdef AGRPC_ASIO_HAS_CANCELLATION_SLOT
template <class RPC>
struct HighLevelClientCancellationTest : test::HighLevelClientTest<RPC>, test::IoContextTest
{
    asio::steady_timer timer{io_context};

    HighLevelClientCancellationTest() { run_io_context_detached(); }
};

// gRPC requests seem to be uncancellable on platforms other than Windows
#ifdef _WIN32
TEST_CASE_TEMPLATE("RPC::request can be cancelled", RPC, test::UnaryRPC, test::GenericUnaryRPC,
                   test::ClientStreamingRPC, test::ServerStreamingRPC, test::BidirectionalStreamingRPC,
                   test::GenericStreamingRPC)
{
    HighLevelClientCancellationTest<RPC> test;
    test.server->Shutdown();
    const auto not_to_exceed = test::one_second_from_now();
    test.timer.expires_at({});
    asio::experimental::make_parallel_group(test.request_rpc(test::ASIO_DEFERRED),
                                            test.timer.async_wait(test::ASIO_DEFERRED))
        .async_wait(asio::experimental::wait_for_one(),
                    [&](auto&&, auto&& rpc, auto&&...)
                    {
                        CHECK_FALSE(rpc.ok());
                        CHECK_EQ(grpc::StatusCode::CANCELLED, get_status_code(rpc));
                        test.server_shutdown.initiate();
                    });
    test.grpc_context.run();
    CHECK_LT(test::now(), not_to_exceed);
}
#endif

struct ClientStreamingReadInitialMetadataCancellation
{
    using RPC = test::ClientStreamingRPC;

    static auto step(HighLevelClientCancellationTest<RPC>&, RPC& rpc)
    {
        return rpc.read_initial_metadata(test::ASIO_DEFERRED);
    }
};
TYPE_TO_STRING(ClientStreamingReadInitialMetadataCancellation);

struct ClientStreamingWriteLastCancellation
{
    using RPC = test::ClientStreamingRPC;

    static auto step(HighLevelClientCancellationTest<RPC>& test, RPC& rpc)
    {
        return rpc.write(test.request, grpc::WriteOptions{}.set_last_message(), test::ASIO_DEFERRED);
    }
};
TYPE_TO_STRING(ClientStreamingWriteLastCancellation);

struct ClientStreamingFinishCancellation
{
    using RPC = test::ClientStreamingRPC;

    static auto step(HighLevelClientCancellationTest<RPC>&, RPC& rpc) { return rpc.finish(test::ASIO_DEFERRED); }
};
TYPE_TO_STRING(ClientStreamingFinishCancellation);

struct ServerStreamingReadInitialMetadataCancellation
{
    using RPC = test::ServerStreamingRPC;

    static auto step(HighLevelClientCancellationTest<RPC>&, RPC& rpc)
    {
        return rpc.read_initial_metadata(test::ASIO_DEFERRED);
    }
};
TYPE_TO_STRING(ServerStreamingReadInitialMetadataCancellation);

struct ServerStreamingReadCancellation
{
    using RPC = test::ServerStreamingRPC;

    static auto step(HighLevelClientCancellationTest<RPC>& test, RPC& rpc)
    {
        return rpc.read(test.response, test::ASIO_DEFERRED);
    }
};
TYPE_TO_STRING(ServerStreamingReadCancellation);

template <class RPCType>
struct BidiStreamingReadInitialMetadataCancellationT
{
    using RPC = RPCType;

    static auto step(HighLevelClientCancellationTest<RPC>&, RPC& rpc)
    {
        return rpc.read_initial_metadata(test::ASIO_DEFERRED);
    }
};

using BidiStreamingReadInitialMetadataCancellation =
    BidiStreamingReadInitialMetadataCancellationT<test::BidirectionalStreamingRPC>;
TYPE_TO_STRING(BidiStreamingReadInitialMetadataCancellation);

using GenericBidiStreamingReadInitialMetadataCancellation =
    BidiStreamingReadInitialMetadataCancellationT<test::GenericStreamingRPC>;
TYPE_TO_STRING(GenericBidiStreamingReadInitialMetadataCancellation);

template <class RPCType>
struct BidiStreamingFinishCancellationT
{
    using RPC = RPCType;

    static auto step(HighLevelClientCancellationTest<RPC>&, RPC& rpc) { return rpc.finish(test::ASIO_DEFERRED); }
};

using BidiStreamingFinishCancellation = BidiStreamingFinishCancellationT<test::BidirectionalStreamingRPC>;
TYPE_TO_STRING(BidiStreamingFinishCancellation);

using GenericBidiStreamingFinishCancellation = BidiStreamingFinishCancellationT<test::GenericStreamingRPC>;
TYPE_TO_STRING(GenericBidiStreamingFinishCancellation);

template <class T>
void test_rpc_step_functions_can_be_cancelled()
{
    HighLevelClientCancellationTest<typename T::RPC> test;
    const auto not_to_exceed = test::one_second_from_now();
    test.spawn_and_run(
        [&](const asio::yield_context& yield)
        {
            test.test_server.request_rpc(yield);
        },
        [&](const asio::yield_context& yield)
        {
            auto rpc = test.request_rpc(yield);
            test.timer.expires_at({});
            asio::experimental::make_parallel_group(test.timer.async_wait(test::ASIO_DEFERRED), T::step(test, rpc))
                .async_wait(asio::experimental::wait_for_one(), yield);
            CHECK_EQ(grpc::StatusCode::CANCELLED, rpc.status_code());
            test.server_shutdown.initiate();
        });
    CHECK_LT(test::now(), not_to_exceed);
}

TEST_CASE_TEMPLATE("RPC::read_initial_metadata can be cancelled", T, ClientStreamingReadInitialMetadataCancellation,
                   ServerStreamingReadInitialMetadataCancellation, BidiStreamingReadInitialMetadataCancellation,
                   GenericBidiStreamingReadInitialMetadataCancellation)
{
    if (grpc::Version() > "1.20.0")
    {
        test_rpc_step_functions_can_be_cancelled<T>();
    }
}

TEST_CASE_TEMPLATE("RPC step functions can be cancelled", T, ClientStreamingWriteLastCancellation,
                   ClientStreamingFinishCancellation, ServerStreamingReadCancellation, BidiStreamingFinishCancellation,
                   GenericBidiStreamingFinishCancellation)
{
    test_rpc_step_functions_can_be_cancelled<T>();
}
#endif