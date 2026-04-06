#include "runtime/observability/structured_log.h"

#include <cstdint>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace framework::observability {

namespace {

std::string ToLower(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

bool NeedsQuoting(std::string_view value) {
    for (const char ch : value) {
        if (std::isspace(static_cast<unsigned char>(ch)) || ch == '"' || ch == '\\' || ch == '=') {
            return true;
        }
    }
    return false;
}

std::uint32_t StableTokenFingerprint(std::string_view value) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char ch : value) {
        hash ^= ch;
        hash *= 16777619u;
    }
    return hash;
}

}  // namespace

EventLogBuilder::EventLogBuilder(LogEvent event) {
    entry_.Add("event", ToString(event));
}

EventLogBuilder& EventLogBuilder::WithHandlerContext(const framework::protocol::HandlerContext& context) {
    AddHandlerContext(&entry_, context);
    return *this;
}

EventLogBuilder& EventLogBuilder::WithErrorCode(LogErrorCode error_code) {
    if (error_code != LogErrorCode::kNone) {
        entry_.Add("error_code", ToString(error_code));
    }
    return *this;
}

EventLogBuilder& EventLogBuilder::WithDetail(std::string_view detail) {
    entry_.Add("detail", detail);
    return *this;
}

EventLogBuilder& EventLogBuilder::WithLatencyMs(std::int64_t latency_ms) {
    entry_.Add("latency_ms", latency_ms);
    return *this;
}

EventLogBuilder& EventLogBuilder::WithGatewayInstance(std::string_view gateway_instance) {
    entry_.Add("gateway.instance.id", gateway_instance);
    return *this;
}

EventLogBuilder& EventLogBuilder::WithUpstreamService(std::string_view upstream_service) {
    entry_.Add("upstream.service", upstream_service);
    return *this;
}

EventLogBuilder& EventLogBuilder::AddField(std::string_view key, std::string_view value) {
    entry_.Add(key, value);
    return *this;
}

EventLogBuilder& EventLogBuilder::AddField(std::string_view key, const std::string& value) {
    entry_.Add(key, value);
    return *this;
}

EventLogBuilder& EventLogBuilder::AddField(std::string_view key, const char* value) {
    entry_.Add(key, value);
    return *this;
}

EventLogBuilder& EventLogBuilder::AddField(std::string_view key, std::int64_t value) {
    entry_.Add(key, value);
    return *this;
}

EventLogBuilder& EventLogBuilder::AddField(std::string_view key, std::uint64_t value) {
    entry_.Add(key, value);
    return *this;
}

std::string EventLogBuilder::Build() const {
    return entry_.Build();
}

void EventLogBuilder::Emit(common::log::LogLevel level, bool sync) const {
    if (sync) {
        common::log::Logger::Instance().LogSync(level, Build());
        return;
    }
    common::log::Logger::Instance().Log(level, Build());
}

std::string ToString(LogEvent event) {
    switch (event) {
    case LogEvent::kGatewayForwardFailed:
        return "gateway_forward_failed";
    case LogEvent::kGatewayForwardRejected:
        return "gateway_forward_rejected";
    case LogEvent::kGatewayForwardStarted:
        return "gateway_forward_started";
    case LogEvent::kGatewayForwardSucceeded:
        return "gateway_forward_succeeded";
    case LogEvent::kGatewayRequestRejected:
        return "gateway_request_rejected";
    case LogEvent::kRequestDispatchContext:
        return "request_dispatch_context";
    case LogEvent::kRequestDispatchFailed:
        return "request_dispatch_failed";
    case LogEvent::kRequestDispatchRejected:
        return "request_dispatch_rejected";
    case LogEvent::kRequestExecutorDrainTimeout:
        return "request_executor_drain_timeout";
    case LogEvent::kRequestExecutorTaskFailed:
        return "request_executor_task_failed";
    case LogEvent::kSessionBindingRestored:
        return "session_binding_restored";
    case LogEvent::kServiceLifecycle:
        return "service_lifecycle";
    case LogEvent::kTransportTlsHandshakeFailed:
        return "transport_tls_handshake_failed";
    case LogEvent::kTrustedGatewayValidationFailed:
        return "trusted_gateway_validation_failed";
    }

    return "unknown_event";
}

std::string ToString(LogErrorCode error_code) {
    switch (error_code) {
    case LogErrorCode::kNone:
        return {};
    case LogErrorCode::kExecutionKeyUnresolved:
        return "execution_key_unresolved";
    case LogErrorCode::kExecutorDrainTimeout:
        return "executor_drain_timeout";
    case LogErrorCode::kExecutorQueueRejected:
        return "executor_queue_rejected";
    case LogErrorCode::kExecutorStopping:
        return "executor_stopping";
    case LogErrorCode::kGatewayContextRewriteFailed:
        return "gateway_context_rewrite_failed";
    case LogErrorCode::kMessageNotSupported:
        return "message_not_supported";
    case LogErrorCode::kRateLimitHit:
        return "rate_limit_hit";
    case LogErrorCode::kRateLimitUnavailable:
        return "rate_limit_unavailable";
    case LogErrorCode::kRequestContextInvalid:
        return "request_context_invalid";
    case LogErrorCode::kRequestDispatchFailed:
        return "request_dispatch_failed";
    case LogErrorCode::kSessionBindingInvalid:
        return "session_binding_invalid";
    case LogErrorCode::kSessionRestoreFailed:
        return "session_restore_failed";
    case LogErrorCode::kTlsCertificateValidationFailed:
        return "tls_certificate_validation_failed";
    case LogErrorCode::kTlsHandshakeFailed:
        return "tls_handshake_failed";
    case LogErrorCode::kTrustedGatewaySignFailed:
        return "trusted_gateway_sign_failed";
    case LogErrorCode::kTrustedGatewayValidationFailed:
        return "trusted_gateway_validation_failed";
    case LogErrorCode::kUpstreamRequestFailed:
        return "upstream_request_failed";
    case LogErrorCode::kUpstreamResponseInvalid:
        return "upstream_response_invalid";
    case LogErrorCode::kUpstreamTimeout:
        return "upstream_timeout";
    }

    return "unknown_error";
}

void LogEntry::Add(std::string_view key, std::string_view value) {
    if (key.empty() || value.empty()) {
        return;
    }

    fields_.push_back(std::string(key) + '=' + EscapeValue(value));
}

void LogEntry::Add(std::string_view key, const std::string& value) {
    Add(key, std::string_view(value));
}

void LogEntry::Add(std::string_view key, const char* value) {
    if (value == nullptr) {
        return;
    }
    Add(key, std::string_view(value));
}

void LogEntry::Add(std::string_view key, std::int64_t value) {
    Add(key, std::to_string(value));
}

void LogEntry::Add(std::string_view key, std::uint64_t value) {
    Add(key, std::to_string(value));
}

std::string LogEntry::Build() const {
    std::ostringstream output;
    for (std::size_t index = 0; index < fields_.size(); ++index) {
        if (index != 0) {
            output << ' ';
        }
        output << fields_[index];
    }
    return output.str();
}

std::string LogEntry::EscapeValue(std::string_view value) {
    if (!NeedsQuoting(value)) {
        return std::string(value);
    }

    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    escaped.push_back('"');
    return escaped;
}

std::string MaskAuthToken(std::string_view auth_token) {
    if (auth_token.empty()) {
        return {};
    }
    if (auth_token.size() <= 8) {
        std::ostringstream output;
        output << "short_" << auth_token.size() << '_';
        output << std::hex << StableTokenFingerprint(auth_token);
        return output.str();
    }
    return std::string(auth_token.substr(0, 4)) + "..." + std::string(auth_token.substr(auth_token.size() - 4));
}

void AddHandlerContext(LogEntry* entry, const framework::protocol::HandlerContext& context) {
    if (entry == nullptr) {
        return;
    }

    entry->Add("trace_id", context.request.trace_id);
    if (context.request.request_id != 0) {
        entry->Add("request_id", context.request.request_id);
    }
    entry->Add("message_id", std::string(common::net::ToString(context.message_id)));

    const auto auth_token = MaskAuthToken(context.request.auth_token);
    if (!auth_token.empty()) {
        entry->Add("auth_token", auth_token);
    }
    if (context.request.player_id != 0) {
        entry->Add("player_id", context.request.player_id);
    }
    if (context.request.account_id != 0) {
        entry->Add("account_id", context.request.account_id);
    }
    if (context.connection_id != 0) {
        entry->Add("connection_id", context.connection_id);
    }
    if (!context.peer_address.empty()) {
        entry->Add("peer.address", context.peer_address);
    }
    if (!context.executor_label.empty()) {
        entry->Add("executor_label", context.executor_label);
    }
    if (context.executor_shard.has_value()) {
        entry->Add("executor_shard", *context.executor_shard);
    }
}

std::string DescribeUpstreamService(common::net::MessageId message_id) {
    switch (message_id) {
    case common::net::MessageId::kLoginRequest:
        return "login";
    case common::net::MessageId::kLoadPlayerRequest:
        return "player";
    case common::net::MessageId::kEnterBattleRequest:
    case common::net::MessageId::kSettleBattleRequest:
    case common::net::MessageId::kGetActiveBattleRequest:
    case common::net::MessageId::kGetRewardGrantStatusRequest:
        return "battle";
    default:
        return {};
    }
}

LogErrorCode ClassifyExecutorError(std::string_view error_message) {
    const auto lowered = ToLower(error_message);
    if (lowered.find("queue limit exceeded") != std::string::npos) {
        return LogErrorCode::kExecutorQueueRejected;
    }
    if (lowered.find("drain timed out") != std::string::npos) {
        return LogErrorCode::kExecutorDrainTimeout;
    }
    if (lowered.find("stopping") != std::string::npos) {
        return LogErrorCode::kExecutorStopping;
    }
    if (lowered.find("execution key") != std::string::npos) {
        return LogErrorCode::kExecutionKeyUnresolved;
    }
    if (lowered.find("not supported") != std::string::npos) {
        return LogErrorCode::kMessageNotSupported;
    }
    return LogErrorCode::kRequestDispatchFailed;
}

LogErrorCode ClassifyTransportError(std::string_view error_message) {
    const auto lowered = ToLower(error_message);
    if (lowered == "timeout") {
        return LogErrorCode::kUpstreamTimeout;
    }
    if (lowered.find("certificate") != std::string::npos || lowered.find("verify") != std::string::npos ||
        lowered.find("unknown ca") != std::string::npos || lowered.find("bad certificate") != std::string::npos ||
        lowered.find("host name mismatch") != std::string::npos || lowered.find("hostname") != std::string::npos) {
        return LogErrorCode::kTlsCertificateValidationFailed;
    }
    if (lowered.rfind("tls ", 0) == 0 || lowered.find("ssl") != std::string::npos ||
        lowered.find("handshake") != std::string::npos || lowered.find("tls") != std::string::npos ||
        lowered.find("alert") != std::string::npos) {
        return LogErrorCode::kTlsHandshakeFailed;
    }
    if (lowered.find("queue limit exceeded") != std::string::npos) {
        return LogErrorCode::kExecutorQueueRejected;
    }
    if (lowered.find("stopping") != std::string::npos) {
        return LogErrorCode::kExecutorStopping;
    }
    return LogErrorCode::kUpstreamRequestFailed;
}

LogErrorCode ClassifySessionFailure(std::string_view reason) {
    const auto lowered = ToLower(reason);
    if (lowered.find("restore failed") != std::string::npos) {
        return LogErrorCode::kSessionRestoreFailed;
    }
    return LogErrorCode::kSessionBindingInvalid;
}

LogErrorCode FromSubmitFailureCode(framework::execution::SubmitFailureCode failure_code) {
    switch (failure_code) {
    case framework::execution::SubmitFailureCode::kNone:
        return LogErrorCode::kNone;
    case framework::execution::SubmitFailureCode::kExecutionKeyUnresolved:
        return LogErrorCode::kExecutionKeyUnresolved;
    case framework::execution::SubmitFailureCode::kNotStarted:
        return LogErrorCode::kRequestDispatchFailed;
    case framework::execution::SubmitFailureCode::kStopping:
        return LogErrorCode::kExecutorStopping;
    case framework::execution::SubmitFailureCode::kQueueLimitExceeded:
        return LogErrorCode::kExecutorQueueRejected;
    }

    return LogErrorCode::kRequestDispatchFailed;
}

LogErrorCode FromTransportFailureCode(framework::transport::TransportFailureCode failure_code) {
    switch (failure_code) {
    case framework::transport::TransportFailureCode::kNone:
        return LogErrorCode::kNone;
    case framework::transport::TransportFailureCode::kResolveFailed:
    case framework::transport::TransportFailureCode::kConnectFailed:
    case framework::transport::TransportFailureCode::kNoUpstreamClients:
    case framework::transport::TransportFailureCode::kWriteFailed:
    case framework::transport::TransportFailureCode::kReadFailed:
    case framework::transport::TransportFailureCode::kProtocolDecodeFailed:
        return LogErrorCode::kUpstreamRequestFailed;
    case framework::transport::TransportFailureCode::kTimeout:
        return LogErrorCode::kUpstreamTimeout;
    case framework::transport::TransportFailureCode::kTlsSetupFailed:
    case framework::transport::TransportFailureCode::kTlsHandshakeFailed:
        return LogErrorCode::kTlsHandshakeFailed;
    case framework::transport::TransportFailureCode::kTlsCertificateValidationFailed:
        return LogErrorCode::kTlsCertificateValidationFailed;
    }

    return LogErrorCode::kUpstreamRequestFailed;
}

}  // namespace framework::observability
