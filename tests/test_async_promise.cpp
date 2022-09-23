#undef CATCH_CONFIG_FAST_COMPILE

#include <asio.hpp>
#include <catch2/catch_test_macros.hpp>
#include <metricq/awaitable.hpp>

#include <iostream>
#include <string>

using namespace metricq;

using namespace std::literals::string_literals;

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

    SECTION("Can be awaited from one future with timeout")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     co_await future.get(std::chrono::milliseconds(100));
                     REQUIRE(true);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_done();
                     co_return;
                 }());
    }

    SECTION("awaiting can timeout")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE_THROWS(co_await future.get(std::chrono::milliseconds(1)));
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
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
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

TEST_CASE("AsyncPromise<int> can be awaited")
{

    asio::io_service io_service;

    metricq::AsyncPromise<int> promise(io_service);

    SECTION("Can be awaited from one future")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE(co_await future.get() == 42);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_result(42);
                     co_return;
                 }());
    }

    SECTION("awaiting can timeout")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE_THROWS(co_await future.get(std::chrono::milliseconds(1)));
                 }());
    }

    SECTION("Can be awaited from two futures")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE(co_await future.get() == 42);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE(co_await future.get() == 42);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_result(42);
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
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
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

TEST_CASE("AsyncPromise<std::string> can be awaited")
{

    asio::io_service io_service;

    metricq::AsyncPromise<std::string> promise(io_service);

    SECTION("Can be awaited from one future")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get();
                     REQUIRE(result == "42");
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_result("42");
                     co_return;
                 }());
    }

    SECTION("awaiting can timeout")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE_THROWS(co_await future.get(std::chrono::milliseconds(1)));
                 }());
    }

    SECTION("Can be awaited from two futures")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get();
                     REQUIRE(result == "42");
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get();
                     REQUIRE(result == "42");
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_result("42");
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
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
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

TEST_CASE("AsyncPromise<std::unique_ptr<std::string>> can be awaited")
{
    asio::io_service io_service;

    metricq::AsyncPromise<std::unique_ptr<std::string>> promise(io_service);

    SECTION("Can be awaited from one future")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get();
                     REQUIRE(*result == "42"s);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_result(std::make_unique<std::string>("42"));
                     co_return;
                 }());
    }

    SECTION("awaiting can timeout")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     REQUIRE_THROWS(co_await future.get(std::chrono::milliseconds(1)));
                 }());
    }

    SECTION("Can be awaited from two futures")
    {
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get();
                     REQUIRE(*result == "42"s);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     auto result = co_await future.get();
                     REQUIRE(*result == "42"s);
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     promise.set_result(std::make_unique<std::string>("42"));
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
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
                 }());
        co_spawn(io_service,
                 [&promise]() -> Awaitable<void>
                 {
                     auto future = promise.get_future();
                     try
                     {
                         co_await future.get();
                     }
                     catch (std::runtime_error& r)
                     {
                         REQUIRE(r.what() == "What a bummer!"s);
                     }
                     catch (...)
                     {
                         REQUIRE(false);
                     }
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
