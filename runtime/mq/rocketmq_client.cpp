#include "runtime/mq/rocketmq_client.h"

#include "apache/rocketmq/v2/service.grpc.pb.h"
#include "runtime/foundation/id/id_generator.h"

#include <grpcpp/grpcpp.h>
#include <unistd.h>

#include <chrono>
#include <cctype>
#include <ctime>
#include <sstream>
#include <utility>

namespace common::mq {

namespace {

namespace rmq = apache::rocketmq::v2;

constexpr char kClientLanguage[] = "CPP";
constexpr char kClientVersion[] = "5.0.4";
constexpr char kProtocolVersion[] = "2.0";

std::string HostFromAddress(const std::string& address) {
    const auto separator = address.rfind(':');
    return separator == std::string::npos ? address : address.substr(0, separator);
}

int PortFromAddress(const std::string& address, int default_port) {
    const auto separator = address.rfind(':');
    if (separator == std::string::npos) {
        return default_port;
    }
    try {
        return std::stoi(address.substr(separator + 1));
    } catch (...) {
        return default_port;
    }
}

std::vector<std::string> SplitEndpoints(const std::string& endpoints) {
    std::vector<std::string> parsed;
    std::string current;
    for (const char ch : endpoints) {
        if (ch == ',' || ch == ';') {
            if (!current.empty()) {
                parsed.push_back(current);
                current.clear();
            }
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        parsed.push_back(current);
    }
    return parsed;
}

rmq::AddressScheme DetectAddressScheme(const std::string& host) {
    if (!host.empty() &&
        host.find_first_not_of("0123456789.") == std::string::npos &&
        host.find('.') != std::string::npos) {
        return rmq::IPv4;
    }
    if (host.find(':') != std::string::npos) {
        return rmq::IPv6;
    }
    return rmq::DOMAIN_NAME;
}

rmq::Endpoints BuildEndpointsMessage(const std::string& endpoints) {
    rmq::Endpoints access_point;
    const auto entries = SplitEndpoints(endpoints);
    if (entries.empty()) {
        access_point.set_scheme(rmq::DOMAIN_NAME);
        return access_point;
    }

    access_point.set_scheme(DetectAddressScheme(HostFromAddress(entries.front())));
    for (const auto& entry : entries) {
        auto* address = access_point.add_addresses();
        address->set_host(HostFromAddress(entry));
        address->set_port(PortFromAddress(entry, 8081));
    }
    return access_point;
}

std::string FirstEndpoint(const std::string& endpoints, int default_port) {
    const auto entries = SplitEndpoints(endpoints);
    if (!entries.empty()) {
        return entries.front();
    }
    return "127.0.0.1:" + std::to_string(default_port);
}

std::string DerivedEndpointsFromNameserver(const std::string& nameserver) {
    return HostFromAddress(nameserver) + ":8081";
}

bool IsSuccessfulStatus(const rmq::Status& status) {
    return status.code() == rmq::Code::OK || status.code() == rmq::Code::CODE_UNSPECIFIED;
}

std::string DescribeStatus(const rmq::Status& status) {
    if (!status.message().empty()) {
        return status.message();
    }
    return "rocketmq status code=" + std::to_string(static_cast<int>(status.code()));
}

void FillTimestamp(::google::protobuf::Timestamp* timestamp) {
    if (timestamp == nullptr) {
        return;
    }
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now);
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now - seconds);
    timestamp->set_seconds(seconds.count());
    timestamp->set_nanos(static_cast<int>(nanos.count()));
}

std::string Hostname() {
    char hostname[256];
    if (::gethostname(hostname, sizeof(hostname)) != 0) {
        return "mobile-game-backend";
    }
    hostname[sizeof(hostname) - 1] = '\0';
    return hostname;
}

std::string BuildClientId(const std::string& prefix) {
    common::id::IdGenerator generator(3);
    const auto effective_prefix = prefix.empty() ? "rocketmq-client" : prefix;
    return effective_prefix + "-" + generator.NextString();
}

void AddMetadata(::grpc::ClientContext* context,
                 const std::string& client_id,
                 const std::string& resource_namespace,
                 common::id::IdGenerator* id_generator) {
    if (context == nullptr || id_generator == nullptr) {
        return;
    }

    context->AddMetadata("x-mq-language", kClientLanguage);
    context->AddMetadata("x-mq-client-id", client_id);
    context->AddMetadata("x-mq-client-version", kClientVersion);
    context->AddMetadata("x-mq-protocol-version", kProtocolVersion);
    context->AddMetadata("x-mq-request-id", id_generator->NextString());
    if (!resource_namespace.empty()) {
        context->AddMetadata("x-mq-namespace", resource_namespace);
    }

    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", &tm);
    context->AddMetadata("x-mq-date-time", buffer);
}

std::shared_ptr<::grpc::Channel> CreateChannel(const RocketMqOptions& options) {
    const auto target = FirstEndpoint(options.endpoints, 8081);
    if (options.tls) {
        return ::grpc::CreateChannel(target, ::grpc::SslCredentials({}));
    }
    return ::grpc::CreateChannel(target, ::grpc::InsecureChannelCredentials());
}

}  // namespace

struct RocketMqProducer::Impl {
    explicit Impl(const RocketMqOptions& options)
        : client_id(BuildClientId(options.producer_group)),
          channel(CreateChannel(options)),
          stub(rmq::MessagingService::NewStub(channel)) {}

    std::string client_id;
    std::shared_ptr<::grpc::Channel> channel;
    std::unique_ptr<rmq::MessagingService::Stub> stub;
    common::id::IdGenerator id_generator{3};
};

struct RocketMqConsumer::Impl {
    explicit Impl(const RocketMqOptions& options)
        : client_id(BuildClientId(options.consumer_group)),
          channel(CreateChannel(options)),
          stub(rmq::MessagingService::NewStub(channel)) {}

    std::string client_id;
    std::shared_ptr<::grpc::Channel> channel;
    std::unique_ptr<rmq::MessagingService::Stub> stub;
    common::id::IdGenerator id_generator{4};
};

RocketMqOptions ReadRocketMqOptions(const common::config::SimpleConfig& config, const std::string& prefix) {
    RocketMqOptions options;
    options.nameserver = config.GetString(prefix + "nameserver", options.nameserver);
    options.endpoints = config.GetString(prefix + "endpoints", "");
    if (options.endpoints.empty()) {
        options.endpoints = DerivedEndpointsFromNameserver(options.nameserver);
    }
    options.topic = config.GetString(prefix + "topic", options.topic);
    options.producer_group = config.GetString(prefix + "producer_group", options.producer_group);
    options.consumer_group = config.GetString(prefix + "consumer_group", options.consumer_group);
    options.resource_namespace = config.GetString(prefix + "namespace", options.resource_namespace);
    options.tag = config.GetString(prefix + "tag", options.tag);
    options.request_timeout_ms = config.GetInt(prefix + "request_timeout_ms", options.request_timeout_ms);
    options.long_polling_timeout_ms = config.GetInt(prefix + "long_polling_timeout_ms", options.long_polling_timeout_ms);
    options.invisible_duration_ms = config.GetInt(prefix + "invisible_duration_ms", options.invisible_duration_ms);
    options.tls = config.GetBool(prefix + "tls", options.tls);
    options.enabled = config.GetBool(prefix + "enabled", options.enabled);
    return options;
}

RocketMqProducer::RocketMqProducer(RocketMqOptions options)
    : options_(std::move(options)),
      impl_(std::make_unique<Impl>(options_)) {}

RocketMqProducer::~RocketMqProducer() = default;

bool RocketMqProducer::Publish(const RocketMqMessage& message, std::string* error_message) {
    if (message.topic.empty() || message.key.empty() || message.payload.empty()) {
        if (error_message != nullptr) {
            *error_message = "rocketmq message requires topic, key and payload";
        }
        return false;
    }
    if (!options_.enabled) {
        return true;
    }
    if (impl_ == nullptr || impl_->stub == nullptr) {
        if (error_message != nullptr) {
            *error_message = "rocketmq producer is not initialized";
        }
        return false;
    }

    ::grpc::ClientContext heartbeat_context;
    AddMetadata(&heartbeat_context, impl_->client_id, options_.resource_namespace, &impl_->id_generator);
    heartbeat_context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(options_.request_timeout_ms));
    rmq::HeartbeatRequest heartbeat_request;
    heartbeat_request.set_client_type(rmq::PRODUCER);
    rmq::HeartbeatResponse heartbeat_response;
    const auto heartbeat_status = impl_->stub->Heartbeat(&heartbeat_context, heartbeat_request, &heartbeat_response);
    if (!heartbeat_status.ok()) {
        if (error_message != nullptr) {
            *error_message = heartbeat_status.error_message();
        }
        return false;
    }

    rmq::QueryRouteRequest route_request;
    route_request.mutable_topic()->set_name(message.topic);
    route_request.mutable_topic()->set_resource_namespace(options_.resource_namespace);
    route_request.mutable_endpoints()->CopyFrom(BuildEndpointsMessage(options_.endpoints));

    ::grpc::ClientContext route_context;
    AddMetadata(&route_context, impl_->client_id, options_.resource_namespace, &impl_->id_generator);
    route_context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(options_.request_timeout_ms));
    rmq::QueryRouteResponse route_response;
    const auto route_status = impl_->stub->QueryRoute(&route_context, route_request, &route_response);
    if (!route_status.ok()) {
        if (error_message != nullptr) {
            *error_message = route_status.error_message();
        }
        return false;
    }
    if (!IsSuccessfulStatus(route_response.status())) {
        if (error_message != nullptr) {
            *error_message = DescribeStatus(route_response.status());
        }
        return false;
    }

    std::int32_t queue_id = 0;
    for (const auto& queue : route_response.message_queues()) {
        if (queue.permission() == rmq::WRITE || queue.permission() == rmq::READ_WRITE) {
            queue_id = queue.id();
            break;
        }
    }

    rmq::SendMessageRequest request;
    auto* msg = request.add_messages();
    msg->mutable_topic()->set_name(message.topic);
    msg->mutable_topic()->set_resource_namespace(options_.resource_namespace);
    auto* system_properties = msg->mutable_system_properties();
    system_properties->add_keys(message.key);
    system_properties->set_message_id("mq-" + impl_->id_generator.NextString());
    system_properties->set_message_type(rmq::NORMAL);
    system_properties->set_body_encoding(rmq::IDENTITY);
    system_properties->set_queue_id(queue_id);
    system_properties->set_born_host(Hostname());
    FillTimestamp(system_properties->mutable_born_timestamp());
    if (!options_.tag.empty() && options_.tag != "*") {
        system_properties->set_tag(options_.tag);
    }
    if (!message.sharding_key.empty()) {
        (*msg->mutable_user_properties())["sharding_key"] = message.sharding_key;
    }
    msg->set_body(message.payload);

    ::grpc::ClientContext context;
    AddMetadata(&context, impl_->client_id, options_.resource_namespace, &impl_->id_generator);
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(options_.request_timeout_ms));
    rmq::SendMessageResponse response;
    const auto status = impl_->stub->SendMessage(&context, request, &response);
    if (!status.ok()) {
        if (error_message != nullptr) {
            *error_message = status.error_message();
        }
        return false;
    }
    if (!IsSuccessfulStatus(response.status()) && response.status().code() != rmq::MULTIPLE_RESULTS) {
        if (error_message != nullptr) {
            *error_message = DescribeStatus(response.status());
        }
        return false;
    }
    if (response.entries().empty()) {
        if (error_message != nullptr) {
            *error_message = "rocketmq send returned empty result";
        }
        return false;
    }
    if (!IsSuccessfulStatus(response.entries(0).status())) {
        if (error_message != nullptr) {
            *error_message = DescribeStatus(response.entries(0).status());
        }
        return false;
    }
    return true;
}

RocketMqConsumer::RocketMqConsumer(RocketMqOptions options)
    : options_(std::move(options)),
      impl_(std::make_unique<Impl>(options_)) {}

RocketMqConsumer::~RocketMqConsumer() = default;

bool RocketMqConsumer::Poll(std::size_t max_messages,
                            std::vector<RocketMqDelivery>* deliveries,
                            std::string* error_message) {
    if (deliveries != nullptr) {
        deliveries->clear();
    }
    if (!options_.enabled) {
        return true;
    }
    if (deliveries == nullptr) {
        if (error_message != nullptr) {
            *error_message = "deliveries output is required";
        }
        return false;
    }
    if (impl_ == nullptr || impl_->stub == nullptr) {
        if (error_message != nullptr) {
            *error_message = "rocketmq consumer is not initialized";
        }
        return false;
    }

    ::grpc::ClientContext heartbeat_context;
    AddMetadata(&heartbeat_context, impl_->client_id, options_.resource_namespace, &impl_->id_generator);
    heartbeat_context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(options_.request_timeout_ms));
    rmq::HeartbeatRequest heartbeat_request;
    heartbeat_request.set_client_type(rmq::SIMPLE_CONSUMER);
    heartbeat_request.mutable_group()->set_name(options_.consumer_group);
    heartbeat_request.mutable_group()->set_resource_namespace(options_.resource_namespace);
    rmq::HeartbeatResponse heartbeat_response;
    const auto heartbeat_status = impl_->stub->Heartbeat(&heartbeat_context, heartbeat_request, &heartbeat_response);
    if (!heartbeat_status.ok()) {
        if (error_message != nullptr) {
            *error_message = heartbeat_status.error_message();
        }
        return false;
    }

    rmq::QueryAssignmentRequest assignment_request;
    assignment_request.mutable_topic()->set_name(options_.topic);
    assignment_request.mutable_topic()->set_resource_namespace(options_.resource_namespace);
    assignment_request.mutable_group()->set_name(options_.consumer_group);
    assignment_request.mutable_group()->set_resource_namespace(options_.resource_namespace);
    assignment_request.mutable_endpoints()->CopyFrom(BuildEndpointsMessage(options_.endpoints));

    ::grpc::ClientContext assignment_context;
    AddMetadata(&assignment_context, impl_->client_id, options_.resource_namespace, &impl_->id_generator);
    assignment_context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(options_.request_timeout_ms));
    rmq::QueryAssignmentResponse assignment_response;
    const auto assignment_status =
        impl_->stub->QueryAssignment(&assignment_context, assignment_request, &assignment_response);
    if (!assignment_status.ok()) {
        if (error_message != nullptr) {
            *error_message = assignment_status.error_message();
        }
        return false;
    }
    if (!IsSuccessfulStatus(assignment_response.status())) {
        if (error_message != nullptr) {
            *error_message = DescribeStatus(assignment_response.status());
        }
        return false;
    }

    for (const auto& assignment : assignment_response.assignments()) {
        rmq::ReceiveMessageRequest request;
        request.mutable_group()->set_name(options_.consumer_group);
        request.mutable_group()->set_resource_namespace(options_.resource_namespace);
        request.mutable_message_queue()->CopyFrom(assignment.message_queue());
        auto* filter = request.mutable_filter_expression();
        filter->set_type(rmq::TAG);
        filter->set_expression(options_.tag.empty() ? "*" : options_.tag);
        request.set_batch_size(static_cast<int32_t>(max_messages));
        request.set_auto_renew(false);
        request.mutable_invisible_duration()->set_seconds(options_.invisible_duration_ms / 1000);
        request.mutable_invisible_duration()->set_nanos((options_.invisible_duration_ms % 1000) * 1000000);
        request.mutable_long_polling_timeout()->set_seconds(options_.long_polling_timeout_ms / 1000);
        request.mutable_long_polling_timeout()->set_nanos((options_.long_polling_timeout_ms % 1000) * 1000000);

        ::grpc::ClientContext context;
        AddMetadata(&context, impl_->client_id, options_.resource_namespace, &impl_->id_generator);
        context.set_deadline(std::chrono::system_clock::now() +
                             std::chrono::milliseconds(options_.request_timeout_ms + options_.long_polling_timeout_ms));
        auto reader = impl_->stub->ReceiveMessage(&context, request);
        if (!reader) {
            continue;
        }

        rmq::ReceiveMessageResponse response;
        while (reader->Read(&response)) {
            if (response.has_status()) {
                if (!IsSuccessfulStatus(response.status())) {
                    if (error_message != nullptr) {
                        *error_message = DescribeStatus(response.status());
                    }
                    reader->Finish();
                    return false;
                }
                continue;
            }
            if (!response.has_message()) {
                continue;
            }

            RocketMqDelivery delivery;
            delivery.topic = response.message().topic().name();
            if (response.message().system_properties().keys_size() > 0) {
                delivery.key = response.message().system_properties().keys(0);
            }
            delivery.message_id = response.message().system_properties().message_id();
            if (response.message().system_properties().has_receipt_handle()) {
                delivery.receipt_handle = response.message().system_properties().receipt_handle();
            }
            delivery.payload = response.message().body();
            deliveries->push_back(std::move(delivery));
        }

        const auto finish_status = reader->Finish();
        if (!finish_status.ok() && deliveries->empty()) {
            if (error_message != nullptr) {
                *error_message = finish_status.error_message();
            }
            return false;
        }
        if (!deliveries->empty()) {
            return true;
        }
    }
    return true;
}

bool RocketMqConsumer::Ack(const RocketMqDelivery& delivery, std::string* error_message) {
    if (!options_.enabled) {
        return true;
    }
    if (delivery.message_id.empty() || delivery.receipt_handle.empty() || delivery.topic.empty()) {
        if (error_message != nullptr) {
            *error_message = "rocketmq delivery requires topic, message_id and receipt_handle";
        }
        return false;
    }

    rmq::AckMessageRequest request;
    request.mutable_group()->set_name(options_.consumer_group);
    request.mutable_group()->set_resource_namespace(options_.resource_namespace);
    request.mutable_topic()->set_name(delivery.topic);
    request.mutable_topic()->set_resource_namespace(options_.resource_namespace);
    auto* entry = request.add_entries();
    entry->set_message_id(delivery.message_id);
    entry->set_receipt_handle(delivery.receipt_handle);

    ::grpc::ClientContext context;
    AddMetadata(&context, impl_->client_id, options_.resource_namespace, &impl_->id_generator);
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(options_.request_timeout_ms));
    rmq::AckMessageResponse response;
    const auto status = impl_->stub->AckMessage(&context, request, &response);
    if (!status.ok()) {
        if (error_message != nullptr) {
            *error_message = status.error_message();
        }
        return false;
    }
    if (!IsSuccessfulStatus(response.status())) {
        if (error_message != nullptr) {
            *error_message = DescribeStatus(response.status());
        }
        return false;
    }
    if (!response.entries().empty() && !IsSuccessfulStatus(response.entries(0).status())) {
        if (error_message != nullptr) {
            *error_message = DescribeStatus(response.entries(0).status());
        }
        return false;
    }
    return true;
}

}  // namespace common::mq
