#include "apps/gateway/rate_limit_service.h"

#include <chrono>
#include <cctype>
#include <string_view>

namespace services::gateway {

namespace {

GatewayRateLimitService::Rule ReadRule(const common::config::SimpleConfig& config, const std::string& prefix) {
    GatewayRateLimitService::Rule rule;
    rule.limit = config.GetInt(prefix + "limit", rule.limit);
    rule.window_seconds = config.GetInt(prefix + "window_seconds", rule.window_seconds);
    return rule;
}

bool IsEnabled(const GatewayRateLimitService::Rule& rule) {
    return rule.limit > 0 && rule.window_seconds > 0;
}

std::string ToLower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

}  // namespace

GatewayRateLimitService::Options GatewayRateLimitService::FromConfig(const common::config::SimpleConfig& config) {
    Options options;
    options.login_ip = ReadRule(config, "gateway.rate_limit.login_ip.");
    options.login_account = ReadRule(config, "gateway.rate_limit.login_account.");
    options.session = ReadRule(config, "gateway.rate_limit.session.");
    options.ip_fallback = ReadRule(config, "gateway.rate_limit.ip_fallback.");
    options.acquire_timeout_ms = config.GetInt("gateway.rate_limit.acquire_timeout_ms", options.acquire_timeout_ms);
    return options;
}

GatewayRateLimitService::GatewayRateLimitService(common::redis::RedisClientPool& redis_pool, Options options)
    : redis_pool_(redis_pool), options_(std::move(options)) {}

GatewayRateLimitService::Decision GatewayRateLimitService::Evaluate(common::net::MessageId message_id,
                                                                    const common::net::RequestContext& request,
                                                                    const std::string& peer_address,
                                                                    const std::string& login_account) const {
    const auto peer_host = ExtractPeerHost(peer_address);
    if (message_id == common::net::MessageId::kLoginRequest) {
        if (const auto decision = Enforce("login_ip", peer_host, options_.login_ip);
            decision.status != Status::kAllowed) {
            return decision;
        }

        if (!login_account.empty()) {
            if (const auto decision = Enforce(
                    "login_account", ToLower(login_account), options_.login_account);
                decision.status != Status::kAllowed) {
                return decision;
            }
        }
        return {};
    }

    if (!request.auth_token.empty()) {
        return Enforce("session", request.auth_token, options_.session);
    }

    return Enforce("ip_fallback", peer_host, options_.ip_fallback);
}

GatewayRateLimitService::Decision GatewayRateLimitService::Enforce(const std::string& scope,
                                                         const std::string& subject,
                                                         const Rule& rule) const {
    if (!IsEnabled(rule) || subject.empty()) {
        return {};
    }

    std::string error_message;
    auto redis = redis_pool_.TryAcquireFor(std::chrono::milliseconds(options_.acquire_timeout_ms), &error_message);
    if (!redis.has_value()) {
        return {Status::kUnavailable, "rate limiter unavailable: " + error_message};
    }

    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch());
    const auto window_index = now.count() / rule.window_seconds;
    const auto key = "rate_limit:" + scope + ':' + subject + ':' + std::to_string(window_index);

    std::int64_t current = 0;
    if (!(*redis)->IncrementWithExpire(key, rule.window_seconds + 1, &current, &error_message)) {
        return {Status::kUnavailable, "rate limiter unavailable: " + error_message};
    }

    if (current > rule.limit) {
        return {Status::kLimited, scope + " rate limit exceeded"};
    }

    return {};
}

std::string GatewayRateLimitService::ExtractPeerHost(const std::string& peer_address) {
    if (peer_address.empty()) {
        return {};
    }

    if (peer_address.front() == '[') {
        const auto closing = peer_address.find(']');
        if (closing != std::string::npos) {
            return peer_address.substr(1, closing - 1);
        }
    }

    const auto last_colon = peer_address.rfind(':');
    if (last_colon != std::string::npos && peer_address.find(':') == last_colon) {
        return peer_address.substr(0, last_colon);
    }

    return peer_address;
}

}  // namespace services::gateway
