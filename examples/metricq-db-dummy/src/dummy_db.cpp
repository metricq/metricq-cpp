// metricq-db-dummy
// Copyright (C) 2021 ZIH, Technische Universitaet Dresden, Federal Republic of Germany
//
// All rights reserved.
//
// This file is part of metricq.
//
// metricq-db-hta is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// metricq-db-hta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with metricq-db-hta.  If not, see <http://www.gnu.org/licenses/>.
#include "dummy_db.hpp"

#include <asio.hpp>
#include <metricq/logger/nitro.hpp>

#include <chrono>
#include <ratio>
#include <stdexcept>

using Log = metricq::logger::nitro::Log;

DummyDb::DummyDb(const std::string& manager_host, const std::string& token)
: metricq::Db(token), signals_(io_service, SIGINT, SIGTERM)
{
    signals_.async_wait([this](auto, auto signal) {
        if (!signal)
        {
            return;
        }
        Log::info() << "Caught signal " << signal << ". Shutdown metricq-db-dummy.";
        stop();
    });

    connect(manager_host);
}

void DummyDb::on_error(const std::string& message)
{
    Log::error() << "Connection to MetricQ failed: " << message;
    signals_.cancel();
}

void DummyDb::on_closed()
{
    Log::debug() << "Connection to MetricQ closed.";
    signals_.cancel();
}

metricq::json DummyDb::on_db_config([[maybe_unused]] const metricq::json& config)
{
    Log::info() << "Received database configuration: " << config;
    return metricq::json::array();
}

void DummyDb::on_db_ready()
{
    Log::info() << "DummyDb is ready!";
}

void DummyDb::on_data(const std::string& metric_name, const metricq::DataChunk& chunk)
{
    Log::info() << "Received datachunk for " << metric_name << " with " << chunk.value_size()
                << " values";
}

metricq::HistoryResponse
DummyDb::on_history([[maybe_unused]] const std::string& id,
                    [[maybe_unused]] const metricq::HistoryRequest& content)
{
    throw std::runtime_error("This database does not serve history request");
}
