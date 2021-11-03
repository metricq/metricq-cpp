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
#include <asio/io_context.hpp>
#include <asio/system_timer.hpp>
#include <asio/use_awaitable.hpp>

#include <asio/defer.hpp>
#include <future>
#include <iostream>

namespace metricq
{
template <typename T>
using awaitable = asio::awaitable<T>;

using use_awaitable_t = asio::use_awaitable_t<>;

static auto use_awaitable = asio::use_awaitable;

template <typename Executor, typename T>
inline void co_spawn(Executor& e, awaitable<T> a)
{
    asio::co_spawn(e, std::move(a), asio::detached);
}

template <typename Executor, typename T, typename Connection>
inline void co_spawn(Executor& e, awaitable<T> a, Connection& connection)
{
    asio::co_spawn(e, std::move(a), [&connection](std::exception_ptr eptr) {
        try
        {
            if (eptr)
            {
                std::rethrow_exception(eptr);
            }
        }
        catch (const std::exception& e)
        {
            connection.on_unhandled_exception(e);
        }
    });
}

[[nodiscard]] inline awaitable<void> wait_for(asio::io_context& ctx, Duration timeout)
{
    asio::system_timer timer(ctx);
    timer.expires_from_now(timeout);
    co_await timer.async_wait(asio::use_awaitable);
}

template <typename T>
[[nodiscard]] awaitable<T> wait_for(asio::io_context& ctx, std::future<T>& fut, Duration timeout)
{
    // At the moment of writing, asio doesn't provide it's own future/promise types (or other
    // coroutine primitives for that matter). Hence, we have to do it on our own.

    // TODO: Make this implementation efficient. This will probably require you to understand
    // ASIO implementation code, or wait for ASIO to support `co_await fut`

    // Right now, this asynchronously polls using the asio timer. Hence, we don't leave
    // the sacred asio lands.

    // I tried to define my own coroutine, which would handle the awaiting of the future, but
    // that went nowhere. The missing puzzle piece is how to "convert" a custom coroutine to
    // something asio understands.

    auto now = Clock::now();

    auto status = fut.wait_for(Duration(0));

    while (status != std::future_status::ready && Clock::now() - now < timeout)
    {
        // this line is the important thing here! Using the asio::system_timer
        // creates an interruption point here that allows the executor to
        // progress other stuff, in particular, it progresses running network
        // operations, which might soon finish the future.
        co_await wait_for(ctx, std::chrono::milliseconds(10));
        status = fut.wait_for(Duration(0));
    }

    if (status == std::future_status::ready)
    {
        // The future is ready, so either it succedded, or an exception was set.
        co_return fut.get();
    }
    else
    {
        // Either we hit the timeout as defined with the parameter, or the future itself
        // ran into a timeout. Whatever that means
        throw TimeoutError("future timed out");
    }
}

} // namespace metricq