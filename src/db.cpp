#include <dataheap2/db.hpp>

#include "util.hpp"

namespace dataheap2
{
Db::Db(const std::string& token) : Sink(token)
{
}

void Db::setup_complete()
{
    register_management_callback(
        "get_metrics", [this](const auto& response) { return get_metrics_callback(response); });
}

void Db::setup_history_queue(const AMQP::QueueCallback& callback)
{
    assert(!data_server_address_.empty());
    assert(!history_queue_.empty());
    if (!data_connection_)
    {
        data_connection_ = std::make_unique<AMQP::TcpConnection>(
            &data_handler_, AMQP::Address(data_server_address_));
    }
    if (!data_channel_)
    {
        data_channel_ = std::make_unique<AMQP::TcpChannel>(data_connection_.get());
        data_channel_->onError(debug_error_cb("db data channel error"));
    }

    data_channel_->declareQueue(history_queue_).onSuccess(callback);
}

void Db::history_callback(const AMQP::Message& incoming_message)
{
    const auto& metric_name = incoming_message.routingkey();
    auto message_string = std::string(incoming_message.body(), incoming_message.bodySize());

    auto content = json::parse(message_string);

    auto response = history_callback(metric_name, content);

    std::string reply_message = response.dump();
    AMQP::Envelope envelope(reply_message.data(), reply_message.size());
    envelope.setCorrelationID(incoming_message.correlationID());
    envelope.setContentType("application/json");

    data_channel_->publish("", incoming_message.replyTo(), envelope);
}

json Db::history_callback(const std::string& id, const json& content)
{
    json response;

    response["target"] = id;

    response["datapoints"] = json::array();

    for (TimeValue tv : history_callback(id, content["range"]["from"], content["range"]["to"],
                                         content["intervalMs"], content["maxDataPoints"]))
    {
        response["datapoints"].push_back({ tv.value, tv.time.time_since_epoch().count() });
    }

    return response;
}

std::vector<TimeValue> history_callback(const std::string& id, const std::string& from_timestamp,
                                        const std::string& to_timestamp, int interval_ms,
                                        int max_datapoints)
{
    return std::vector<TimeValue>();
}
} // namespace dataheap2
