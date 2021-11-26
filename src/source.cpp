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

#include "log.hpp"

#include <metricq/datachunk.pb.h>
#include <metricq/json.hpp>
#include <metricq/source.hpp>

#include <amqpcpp.h>

#include <iostream>
#include <memory>
#include <string>

#include <cmath>

namespace metricq
{

Source::Source(const std::string& token) : DataClient(token)
{
    register_rpc_callback("config", [this](const json& config) -> Awaitable<json> {
        co_await on_source_config(config);
        co_await declare_metrics();
        co_return json::object();
    });
}

Awaitable<void> Source::on_connected()
{
    auto response = co_await rpc("source.register");
    co_await on_register_response(response);
}

void Source::send(const std::string& id, const DataChunk& dc)
{
    data_channel_->publish(data_exchange_, id, dc.SerializeAsString());
}

void Source::send(const std::string& id, TimeValue tv)
{
    // TODO evaluate optimization of string construction
    data_channel_->publish(data_exchange_, id, DataChunk(tv).SerializeAsString());
}

Awaitable<void> Source::on_register_response(const json& response)
{
    co_await data_config(response);

    assert(data_exchange_.empty());

    // TODO: check if there's a better error to throw than what at() and get() throw in case any of
    // the required fields is missing.
    data_exchange_ = response.at("dataExchange").get<std::string>();
    co_await on_source_config(response.at("config"));
}

Awaitable<void> Source::on_data_channel_ready()
{
    co_await on_source_ready();
    co_await declare_metrics();
}

Awaitable<void> Source::declare_metrics()
{
    if (metrics_.empty())
    {
        co_return;
    }

    json payload;
    for (auto& metric : metrics_)
    {
        if (std::isnan(metric.second.metadata.chunk_size()))
        {
            metric.second.metadata.chunk_size(metric.second.chunk_size());
        }

        payload["metrics"][metric.second.id()] = metric.second.metadata.json();
    }

    co_await rpc("source.declare_metrics", payload);
}

void Source::clear_metrics()
{
    metrics_.clear();
}
} // namespace metricq
