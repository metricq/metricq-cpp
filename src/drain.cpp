// Copyright (c) 2018, ZIH,
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
#include <metricq/drain.hpp>

#include "log.hpp"
#include "util.hpp"

namespace metricq
{
Drain::~Drain()
{
}

Awaitable<void> Drain::on_connected()
{
    assert(!metrics_.empty());

    auto payload = json{ { "dataQueue", data_queue() }, { "metrics", metrics_ } };
    auto response = co_await rpc("sink.unsubscribe", payload);

    assert(!data_queue().empty());

    co_await sink_config(response);
}

Awaitable<void> Drain::on_data(const AMQP::Message& message, uint64_t delivery_tag,
                               bool redelivered)
{
    if (message.typeName() == "end")
    {
        data_channel_->ack(delivery_tag);
        log::debug("received end message");

        auto payload = json{ { "dataQueue", data_queue() } };
        co_await rpc("sink.release", payload);

        close();

        co_return;
    }

    co_await Sink::on_data(message, delivery_tag, redelivered);
}
} // namespace metricq
