#include "runtime/observability/structured_log.h"

#include <iostream>

namespace {

bool Expect(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }

    std::cerr << message << '\n';
    return false;
}

}  // namespace

int main() {
    framework::protocol::HandlerContext context;
    context.request.trace_id = "trace-42";
    context.request.request_id = 42;
    context.request.auth_token = "auth-token-abcdef";
    context.request.player_id = 20001;
    context.connection_id = 99;
    context.executor_label = "forward-shard-1";
    context.message_id = common::net::MessageId::kLoadPlayerRequest;

    framework::observability::LogEntry entry;
    entry.Add("event", "gateway_forward_failed");
    framework::observability::AddHandlerContext(&entry, context);
    entry.Add("upstream.service", framework::observability::DescribeUpstreamService(context.message_id));
    entry.Add("gateway.instance.id", "gateway-1");
    entry.Add("error_code",
              framework::observability::ToString(
                  framework::observability::ClassifyTransportError("timeout")));
    entry.Add("detail", "forwarding request to upstream");
    entry.Add("latency_ms", static_cast<std::int64_t>(123));

    const auto rendered = entry.Build();
    if (!Expect(rendered.find("trace_id=trace-42") != std::string::npos, "expected trace_id field")) {
        return 1;
    }
    if (!Expect(rendered.find("message_id=LOAD_PLAYER_REQUEST") != std::string::npos, "expected message_id field")) {
        return 1;
    }
    if (!Expect(rendered.find("auth_token=auth...cdef") != std::string::npos, "expected masked auth_token field")) {
        return 1;
    }
    if (!Expect(framework::observability::MaskAuthToken("short") != "short",
                "expected short tokens to be masked instead of logged verbatim")) {
        return 1;
    }
    if (!Expect(rendered.find("executor_label=forward-shard-1") != std::string::npos,
                "expected executor_label field")) {
        return 1;
    }
    if (!Expect(rendered.find("upstream.service=player") != std::string::npos, "expected upstream service field")) {
        return 1;
    }
    if (!Expect(rendered.find("gateway.instance.id=gateway-1") != std::string::npos,
                "expected gateway instance field")) {
        return 1;
    }
    if (!Expect(rendered.find("error_code=upstream_timeout") != std::string::npos,
                "expected upstream timeout classification")) {
        return 1;
    }
    if (!Expect(rendered.find("detail=\"forwarding request to upstream\"") != std::string::npos,
                "expected quoted detail field")) {
        return 1;
    }
    if (!Expect(framework::observability::ToString(framework::observability::LogEvent::kRequestExecutorTaskFailed) ==
                    "request_executor_task_failed",
                "expected request executor task failed event name")) {
        return 1;
    }
    if (!Expect(framework::observability::ToString(framework::observability::LogEvent::kServiceLifecycle) ==
                    "service_lifecycle",
                "expected service lifecycle event name")) {
        return 1;
    }
    if (!Expect(framework::observability::ClassifyTransportError(
                    "tls handshake failed: certificate verify failed") ==
                    framework::observability::LogErrorCode::kTlsCertificateValidationFailed,
                "expected certificate validation failure classification")) {
        return 1;
    }
    if (!Expect(framework::observability::ClassifyTransportError("tlsv1 alert unknown ca") ==
                    framework::observability::LogErrorCode::kTlsCertificateValidationFailed,
                "expected unknown ca to classify as certificate validation failure")) {
        return 1;
    }
    if (!Expect(framework::observability::ClassifyExecutorError("request executor queue limit exceeded") ==
                    framework::observability::LogErrorCode::kExecutorQueueRejected,
                "expected executor queue rejection classification")) {
        return 1;
    }
    if (!Expect(framework::observability::ClassifySessionFailure("session restore failed") ==
                    framework::observability::LogErrorCode::kSessionRestoreFailed,
                "expected session restore failure classification")) {
        return 1;
    }

    return 0;
}
