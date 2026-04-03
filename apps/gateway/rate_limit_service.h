#pragma once

#include "runtime/foundation/config/simple_config.h"
#include "runtime/protocol/message_id.h"
#include "runtime/protocol/request_context.h"
#include "runtime/storage/redis/redis_client_pool.h"

#include <string>

namespace services::gateway {

class GatewayRateLimitService {
public:
    struct Rule {
        int limit = 0;
        int window_seconds = 0;
    };

    struct Options {
        Rule login_ip;
        Rule login_account;
        Rule session;
        Rule ip_fallback;
        int acquire_timeout_ms = 100;
    };

    enum class Status {
        kAllowed,
        kLimited,
        kUnavailable,
    };

    struct Decision {
        Status status = Status::kAllowed;
        std::string reason;
    };

    static Options FromConfig(const common::config::SimpleConfig& config);

    GatewayRateLimitService(common::redis::RedisClientPool& redis_pool, Options options);

    [[nodiscard]] Decision Evaluate(common::net::MessageId message_id,
                                    const common::net::RequestContext& request,
                                    const std::string& peer_address,
                                    const std::string& login_account = {}) const;

private:
    [[nodiscard]] Decision Enforce(const std::string& scope,
                                   const std::string& subject,
                                   const Rule& rule) const;
    [[nodiscard]] static std::string ExtractPeerHost(const std::string& peer_address);

    common::redis::RedisClientPool& redis_pool_;
    Options options_;
};

}  // namespace services::gateway
