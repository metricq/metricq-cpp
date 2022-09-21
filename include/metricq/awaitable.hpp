// Copyright (c) 2021, ZIH,
// Technische Universitaet Dresden,
// Federal Republic of Germany
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright notice,
//       this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright notice,
//       this list of conditions and the following disclaimer in the documentation
//       and/or other materials provided with the distribution.
//     * Neither the name of metricq nor the names of its contributors
//       may be used to endorse or promote products derived from this software
//       without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <metricq/chrono.hpp>
#include <metricq/exception.hpp>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/experimental/promise.hpp>
#include <asio/io_context.hpp>
#include <asio/system_timer.hpp>
#include <asio/use_awaitable.hpp>

#include <exception>
#include <future>
#include <iostream>
#include <optional>

namespace metricq
{
template <typename T>
using Awaitable = asio::awaitable<T>;

static auto use_awaitable = asio::use_awaitable;

template <typename Executor, typename T>
inline void co_spawn(Executor& e, Awaitable<T> a)
{
    asio::co_spawn(e, std::move(a), asio::detached);
}

template <typename Executor, typename F>
inline void co_spawn(Executor& e, F&& f)
{
    asio::co_spawn(e, std::forward<F>(f), asio::detached);
}

namespace detail
{
    template <typename ExceptionHandler>
    class CompletionHandler
    {
    public:
        CompletionHandler(ExceptionHandler& handler) : handler(handler)
        {
        }

        void operator()(std::exception_ptr eptr)
        {
            try
            {
                if (eptr)
                {
                    std::rethrow_exception(eptr);
                }
            }
            catch (const std::exception& e)
            {
                handler.on_unhandled_exception(e);
            }
        }

        template <typename T>
        void operator()(std::exception_ptr eptr, T&&)
        {
            (*this)(eptr);
        }

    private:
        ExceptionHandler& handler;
    };
} // namespace detail

template <typename Executor, typename T, typename ExceptionHandler>
inline void co_spawn(Executor& e, Awaitable<T> a, ExceptionHandler& handler)
{
    asio::co_spawn(e, std::move(a), detail::CompletionHandler<ExceptionHandler>(handler));
}

template <typename Executor, typename F, typename ExceptionHandler>
inline void co_spawn(Executor& e, F&& f, ExceptionHandler& handler)
{
    asio::co_spawn(e, std::forward<F>(f), detail::CompletionHandler<ExceptionHandler>(handler));
}

template <typename T>
class AsyncPromise;

template <typename T>
class AsyncFuture
{
    AsyncFuture(asio::experimental::promise<void(std::optional<T>, std::exception_ptr)>& promise)
    : promise_(promise)
    {
    }

public:
    Awaitable<T> get()
    {
        auto result = co_await promise_.async_wait(use_awaitable);

        auto eptr = std::get<std::exception_ptr>(result);
        auto value = std::get<std::optional<T>>(result);

        if (eptr)
        {
            assert(!value.has_value());
            std::rethrow_exception(eptr);
        }

        assert(value.has_value());

        co_return value.value();
    }

    Awaitable<T> get(Duration timeout)
    {
        asio::system_timer timer(promise_.get_executor());
        timer.expires_from_now(timeout);

        using asio::experimental::awaitable_operators::operator||;

        auto result = co_await (get() || timer.async_wait(use_awaitable));

        timer.cancel();
        promise_.cancel();

        if (std::holds_alternative<T>(result))
        {
            co_return std::get<T>(result);
        }
        else
        {
            throw TimeoutError("Promise didn't finish within the timeout");
        }
    }

private:
    friend class AsyncPromise<T>;

    asio::experimental::promise<void(std::optional<T>, std::exception_ptr)>& promise_;
};

template <>
class AsyncFuture<void>
{
    AsyncFuture(asio::experimental::promise<void(std::exception_ptr)>& promise) : promise_(promise)
    {
    }

public:
    Awaitable<void> get()
    {
        co_await promise_.async_wait(use_awaitable);
    }

    Awaitable<void> get(Duration timeout)
    {
        asio::system_timer timer(promise_.get_executor());
        timer.expires_from_now(timeout);

        using asio::experimental::awaitable_operators::operator||;

        auto result = co_await (get() || timer.async_wait(use_awaitable));

        timer.cancel();
        promise_.cancel();

        if (result.index() == 0)
        {
            co_return;
        }
        else
        {
            throw TimeoutError("Promise didn't finish within the timeout");
        }
    }

private:
    friend class AsyncPromise<void>;

    asio::experimental::promise<void(std::exception_ptr)>& promise_;
};

template <typename T>
class AsyncPromise
{
public:
    AsyncPromise(asio::io_context& io_context)
    : handler_(io_context.get_executor()), promise_(handler_.make_promise())
    {
    }

    void set_exception(std::exception_ptr eptr)
    {
        handler_({}, eptr);
    }

    void set_value(T&& value)
    {
        handler_(std::move(value), {});
    }

    void set_value(const T& value)
    {
        handler_(value, {});
    }

    AsyncFuture<T> get_future()
    {
        return { promise_ };
    }

    void cancel()
    {
        promise_.cancel();
    }

private:
    using AsioPromiseHandler =
        asio::experimental::detail::promise_handler<void(std::optional<T>, std::exception_ptr)>;

    AsioPromiseHandler handler_;
    asio::experimental::promise<void(std::optional<T>, std::exception_ptr)> promise_;
};

template <>
class AsyncPromise<void>
{
public:
    AsyncPromise(asio::io_context& io_context)
    : handler_(io_context.get_executor()), promise_(handler_.make_promise())
    {
    }

    void set_exception(std::exception_ptr eptr)
    {
        handler_(eptr);
    }

    template <typename Exception>
    void set_exception(Exception&& e)
    {
        set_exception(std::make_exception_ptr(std::move(e)));
    }

    void set_done()
    {
        handler_({});
    }

    AsyncFuture<void> get_future()
    {
        return { promise_ };
    }

    void cancel()
    {
        promise_.cancel();
    }

private:
    using AsioPromiseHandler =
        asio::experimental::detail::promise_handler<void(std::exception_ptr)>;

    AsioPromiseHandler handler_;
    asio::experimental::promise<void(std::exception_ptr)> promise_;
};

[[nodiscard]] inline Awaitable<void> wait_until(TimePoint timeout)
{
    asio::system_timer timer(co_await asio::this_coro::executor);
    timer.expires_at(timeout);
    co_await timer.async_wait(asio::use_awaitable);
}

[[nodiscard]] inline Awaitable<void> wait_for(Duration timeout)
{
    asio::system_timer timer(co_await asio::this_coro::executor);
    timer.expires_from_now(timeout);
    co_await timer.async_wait(asio::use_awaitable);
}

} // namespace metricq
