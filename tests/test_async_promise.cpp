#include <asio.hpp>
#include <catch2/catch_test_macros.hpp>
#include <metricq/awaitable.hpp>

using namespace metricq;

TEST_CASE("AsyncPromise<void> can be awaited")
{

    asio::io_service io_service;

    metricq::AsyncPromise<void> promise(io_service);

    SECTION("Can be awaited from one future")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     co_await future.get();
                     REQUIRE(true);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_done();
                     co_return;
                 }());
    }

    SECTION("Can be awaited from two futures")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     co_await future.get();
                     REQUIRE(true);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     co_await future.get();
                     REQUIRE(true);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_done();
                     co_return;
                 }());
    }

    SECTION("Can pass an exception to the future context")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE_THROWS(co_await future.get());
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_exception(std::runtime_error("What a bummer!"));
                     co_return;
                 }());
    }

    SECTION("Can pass an exception to both future contexts")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE_THROWS(co_await future.get());
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE_THROWS(co_await future.get());
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_exception(std::runtime_error("What a bummer!"));
                     co_return;
                 }());
    }

    io_service.run();
}
