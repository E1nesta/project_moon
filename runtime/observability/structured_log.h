#pragma once

#include "runtime/foundation/log/logger.h"
#include "runtime/execution/sharded_request_executor.h"
#include "runtime/protocol/handler_context.h"
#include "runtime/transport/transport_client.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace framework::observability {

enum class LogEvent {
    kGatewayForwardFailed,
    kGatewayForwardRejected,
    kGatewayForwardStarted,
    kGatewayForwardSucceeded,
    kGatewayRequestRejected,
    kRequestDispatchContext,
    kRequestDispatchFailed,
    kRequestDispatchRejected,
    kRequestExecutorDrainTimeout,
    kRequestExecutorTaskFailed,
    kSessionBindingRestored,
    kServiceLifecycle,
    kTransportTlsHandshakeFailed,
    kTrustedGatewayValidationFailed,
};

enum class LogErrorCode {
    kNone,
    kExecutionKeyUnresolved,
    kExecutorDrainTimeout,
    kExecutorQueueRejected,
    kExecutorStopping,
    kGatewayContextRewriteFailed,
    kMessageNotSupported,
    kRateLimitHit,
    kRateLimitUnavailable,
    kRequestContextInvalid,
    kRequestDispatchFailed,
    kSessionBindingInvalid,
    kSessionRestoreFailed,
    kTlsCertificateValidationFailed,
    kTlsHandshakeFailed,
    kTrustedGatewaySignFailed,
    kTrustedGatewayValidationFailed,
    kUpstreamRequestFailed,
    kUpstreamResponseInvalid,
    kUpstreamTimeout,
};

class LogEntry {
public:
    void Add(std::string_view key, std::string_view value);
    void Add(std::string_view key, const std::string& value);
    void Add(std::string_view key, const char* value);
    void Add(std::string_view key, std::int64_t value);
    void Add(std::string_view key, std::uint64_t value);
    [[nodiscard]] std::string Build() const;

private:
    [[nodiscard]] static std::string EscapeValue(std::string_view value);

    std::vector<std::string> fields_;
};

class EventLogBuilder {
public:
    explicit EventLogBuilder(LogEvent event);

    EventLogBuilder& WithHandlerContext(const framework::protocol::HandlerContext& context);
    EventLogBuilder& WithErrorCode(LogErrorCode error_code);
    EventLogBuilder& WithDetail(std::string_view detail);
    EventLogBuilder& WithLatencyMs(std::int64_t latency_ms);
    EventLogBuilder& WithGatewayInstance(std::string_view gateway_instance);
    EventLogBuilder& WithUpstreamService(std::string_view upstream_service);
    EventLogBuilder& AddField(std::string_view key, std::string_view value);
    EventLogBuilder& AddField(std::string_view key, const std::string& value);
    EventLogBuilder& AddField(std::string_view key, const char* value);
    EventLogBuilder& AddField(std::string_view key, std::int64_t value);
    EventLogBuilder& AddField(std::string_view key, std::uint64_t value);
    [[nodiscard]] std::string Build() const;
    void Emit(common::log::LogLevel level, bool sync = false) const;

private:
    LogEntry entry_;
};

[[nodiscard]] std::string ToString(LogEvent event);
[[nodiscard]] std::string ToString(LogErrorCode error_code);
[[nodiscard]] std::string MaskAuthToken(std::string_view auth_token);
void AddHandlerContext(LogEntry* entry, const framework::protocol::HandlerContext& context);
[[nodiscard]] std::string DescribeUpstreamService(common::net::MessageId message_id);
[[nodiscard]] LogErrorCode ClassifyExecutorError(std::string_view error_message);
[[nodiscard]] LogErrorCode ClassifyTransportError(std::string_view error_message);
[[nodiscard]] LogErrorCode ClassifySessionFailure(std::string_view reason);
[[nodiscard]] LogErrorCode FromSubmitFailureCode(framework::execution::SubmitFailureCode failure_code);
[[nodiscard]] LogErrorCode FromTransportFailureCode(framework::transport::TransportFailureCode failure_code);

}  // namespace framework::observability
