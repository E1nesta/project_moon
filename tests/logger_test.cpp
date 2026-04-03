#include "runtime/foundation/log/logger.h"
#include "runtime/observability/structured_log.h"

#include <iostream>
#include <sstream>
#include <string>

namespace {

bool Expect(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }

    std::cerr << message << '\n';
    return false;
}

class StreamCapture {
public:
    StreamCapture(std::ostream& stdout_stream, std::ostream& stderr_stream)
        : stdout_stream_(stdout_stream),
          stderr_stream_(stderr_stream),
          stdout_previous_(stdout_stream.rdbuf(stdout_buffer_.rdbuf())),
          stderr_previous_(stderr_stream.rdbuf(stderr_buffer_.rdbuf())) {}

    ~StreamCapture() {
        stdout_stream_.rdbuf(stdout_previous_);
        stderr_stream_.rdbuf(stderr_previous_);
    }

    [[nodiscard]] std::string stdout_str() const {
        return stdout_buffer_.str();
    }

    [[nodiscard]] std::string stderr_str() const {
        return stderr_buffer_.str();
    }

private:
    std::ostream& stdout_stream_;
    std::ostream& stderr_stream_;
    std::ostringstream stdout_buffer_;
    std::ostringstream stderr_buffer_;
    std::streambuf* stdout_previous_ = nullptr;
    std::streambuf* stderr_previous_ = nullptr;
};

}  // namespace

int main() {
    auto& logger = common::log::Logger::Instance();
    logger.Flush();
    logger.SetServiceName("logger_test");
    logger.SetServiceInstanceId("logger-test-1");
    logger.SetEnvironment("dev");
    logger.SetLogFormat(common::log::LogFormat::kText);

    if (!Expect(logger.SetMinLogLevel("debug"), "expected debug log level to be accepted")) {
        return 1;
    }
    if (!Expect(logger.SetLogFormat("auto"), "expected auto log format to be accepted")) {
        return 1;
    }
    if (!Expect(!logger.SetLogFormat("yaml"), "expected invalid log format to be rejected")) {
        return 1;
    }
    if (!Expect(!logger.SetMinLogLevel("verbose"), "expected invalid log level to be rejected")) {
        return 1;
    }

    {
        StreamCapture capture(std::cout, std::cerr);
        logger.SetEnvironment("dev");
        logger.SetLogFormat(common::log::LogFormat::kText);
        logger.SetMinLogLevel(common::log::LogLevel::kInfo);
        logger.LogSync(common::log::LogLevel::kDebug, "suppressed debug message");
        logger.LogSync(common::log::LogLevel::kInfo, "plain startup message");
        logger.LogSync(common::log::LogLevel::kWarn, "plain warning message");

        const auto stdout_output = capture.stdout_str();
        const auto stderr_output = capture.stderr_str();
        if (!Expect(stdout_output.find("suppressed debug message") == std::string::npos,
                    "expected debug log to be filtered by info threshold")) {
            return 1;
        }
        if (!Expect(stdout_output.find("severity=INFO") != std::string::npos, "expected info severity on stdout")) {
            return 1;
        }
        if (!Expect(stdout_output.find("service.name=logger_test") != std::string::npos,
                    "expected service.name field on stdout")) {
            return 1;
        }
        if (!Expect(stdout_output.find("service.instance.id=logger-test-1") != std::string::npos,
                    "expected service.instance.id field on stdout")) {
            return 1;
        }
        if (!Expect(stdout_output.find("deployment.environment=dev") != std::string::npos,
                    "expected deployment.environment field on stdout")) {
            return 1;
        }
        if (!Expect(stdout_output.find("message=\"plain startup message\"") != std::string::npos,
                    "expected plain info message in text format")) {
            return 1;
        }
        if (!Expect(stderr_output.find("severity=WARN") != std::string::npos, "expected warn severity on stderr")) {
            return 1;
        }
        if (!Expect(stderr_output.find("message=\"plain warning message\"") != std::string::npos,
                    "expected warn message on stderr")) {
            return 1;
        }
        if (!Expect(stderr_output.find('{') == std::string::npos, "expected text logs to avoid JSON syntax")) {
            return 1;
        }
    }

    {
        StreamCapture capture(std::cout, std::cerr);
        logger.SetEnvironment("prod");
        logger.SetLogFormat(common::log::LogFormat::kAuto);
        logger.SetMinLogLevel(common::log::LogLevel::kDebug);
        framework::observability::EventLogBuilder(framework::observability::LogEvent::kGatewayForwardSucceeded)
            .AddField("trace_id", "trace-42")
            .AddField("message_id", "LOAD_PLAYER_REQUEST")
            .AddField("latency_ms", static_cast<std::int64_t>(12))
            .Emit(common::log::LogLevel::kInfo, true);

        const auto stdout_output = capture.stdout_str();
        if (!Expect(!stdout_output.empty() && stdout_output.front() == '{',
                    "expected prod auto format to emit JSON")) {
            return 1;
        }
        if (!Expect(stdout_output.find("\"severity\":\"INFO\"") != std::string::npos,
                    "expected JSON severity field")) {
            return 1;
        }
        if (!Expect(stdout_output.find("\"service.name\":\"logger_test\"") != std::string::npos,
                    "expected JSON service.name field")) {
            return 1;
        }
        if (!Expect(stdout_output.find("\"service.instance.id\":\"logger-test-1\"") != std::string::npos,
                    "expected JSON service.instance.id field")) {
            return 1;
        }
        if (!Expect(stdout_output.find("\"deployment.environment\":\"prod\"") != std::string::npos,
                    "expected JSON deployment.environment field")) {
            return 1;
        }
        if (!Expect(stdout_output.find("\"event\":\"gateway_forward_succeeded\"") != std::string::npos,
                    "expected JSON event field")) {
            return 1;
        }
        if (!Expect(stdout_output.find("\"latency_ms\":12") != std::string::npos,
                    "expected JSON numeric field to stay numeric")) {
            return 1;
        }
    }

    {
        StreamCapture capture(std::cout, std::cerr);
        logger.SetEnvironment("dev");
        logger.SetLogFormat(common::log::LogFormat::kText);
        logger.SetMinLogLevel(common::log::LogLevel::kInfo);
        framework::observability::EventLogBuilder(framework::observability::LogEvent::kServiceLifecycle)
            .AddField("action", "listening")
            .AddField("listen.host", "127.0.0.1")
            .AddField("listen.port", static_cast<std::int64_t>(7000))
            .WithDetail("service listening")
            .Emit(common::log::LogLevel::kInfo, true);

        const auto stdout_output = capture.stdout_str();
        if (!Expect(stdout_output.find("event=service_lifecycle") != std::string::npos,
                    "expected service lifecycle event in text logs")) {
            return 1;
        }
        if (!Expect(stdout_output.find("action=listening") != std::string::npos,
                    "expected lifecycle action field")) {
            return 1;
        }
        if (!Expect(stdout_output.find("listen.host=127.0.0.1") != std::string::npos,
                    "expected lifecycle listen host field")) {
            return 1;
        }
        if (!Expect(stdout_output.find("listen.port=7000") != std::string::npos,
                    "expected lifecycle listen port field")) {
            return 1;
        }
        if (!Expect(stdout_output.find("detail=\"service listening\"") != std::string::npos,
                    "expected lifecycle detail field")) {
            return 1;
        }
    }

    logger.SetEnvironment("dev");
    logger.SetLogFormat(common::log::LogFormat::kText);
    logger.SetMinLogLevel(common::log::LogLevel::kInfo);
    return 0;
}
