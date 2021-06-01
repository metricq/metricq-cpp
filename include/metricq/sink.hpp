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

#pragma once

#include <metricq/data_client.hpp>
#include <metricq/datachunk.pb.h>
#include <metricq/json.hpp>
#include <metricq/metadata.hpp>
#include <metricq/types.hpp>

#include <memory>
#include <optional>
#include <string>

namespace metricq
{

class Sink : public DataClient
{
public:
    explicit Sink(const std::string& token, bool add_uuid = false);
    virtual ~Sink() = 0;

protected:
    /**
     * override this only in special cases where you manually handle acknowledgements
     * or do something after acks
     */
    virtual void on_data(const AMQP::Message& message, uint64_t delivery_tag, bool redelivered);
    /**
     * override this to handle chunks efficiently
     * if you do, you don't need to override data_callback(std::string, TimeValue)
     */
    virtual void on_data(const std::string& id, const DataChunk& chunk);
    /**
     * override this to handle individual values
     */
    virtual void on_data(const std::string& id, TimeValue tv);

    void sink_config(const json& config);

    void update_metadata(const json& config);

    void subscribe(const std::vector<std::string>& metrics);

    void subscribe(const std::vector<std::string>& metrics, Duration expires);

    void data_queue(const std::string& name);

    const std::string& data_queue() const
    {
        return data_queue_;
    }

private:
    // let's hope the child classes never need to deal with this and the generic callback is
    // sufficient
    void setup_data_queue();
    void setup_data_consumer(const std::string& name, int message_count, int consumer_count);

protected:
    // This is the data_queue that will be used for data consumption
    std::string data_queue_;
    // Stored permanently to avoid expensive allocations
    DataChunk data_chunk_;
    std::unordered_map<std::string, Metadata> metadata_;

private:
    // The data_queue is already setup for the data consumption
    bool is_data_queue_set_up_ = false;
};
} // namespace metricq
