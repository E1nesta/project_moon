#include "runtime/storage/redis/redis_client_pool.h"

#include <stdexcept>
#include <utility>

namespace common::redis {

RedisClientPool::Lease::Lease(RedisClientPool* pool, std::unique_ptr<RedisClient> client)
    : pool_(pool), client_(std::move(client)) {}

RedisClientPool::Lease::Lease(Lease&& other) noexcept
    : pool_(other.pool_), client_(std::move(other.client_)) {
    other.pool_ = nullptr;
}

RedisClientPool::Lease& RedisClientPool::Lease::operator=(Lease&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    Reset();
    pool_ = other.pool_;
    client_ = std::move(other.client_);
    other.pool_ = nullptr;
    return *this;
}

RedisClientPool::Lease::~Lease() {
    Reset();
}

RedisClient& RedisClientPool::Lease::operator*() const {
    return *client_;
}

RedisClient* RedisClientPool::Lease::operator->() const {
    return client_.get();
}

bool RedisClientPool::Lease::HasValue() const {
    return client_ != nullptr;
}

void RedisClientPool::Lease::Reset() {
    if (pool_ != nullptr && client_ != nullptr) {
        pool_->Return(std::move(client_));
    }
    pool_ = nullptr;
}

RedisClientPool::RedisClientPool(ConnectionOptions options, std::size_t pool_size)
    : options_(std::move(options)), pool_size_(pool_size == 0 ? std::size_t{1} : pool_size) {}

bool RedisClientPool::Initialize(std::string* error_message) {
    std::lock_guard lock(mutex_);
    if (!available_.empty()) {
        return true;
    }

    available_.reserve(pool_size_);
    for (std::size_t index = 0; index < pool_size_; ++index) {
        auto client = std::make_unique<RedisClient>(options_);
        if (!client->Ping(error_message)) {
            return false;
        }
        available_.push_back(std::move(client));
    }
    return true;
}

RedisClientPool::Lease RedisClientPool::Acquire() {
    std::unique_lock lock(mutex_);
    if (!cv_.wait_for(lock, std::chrono::seconds(5), [this] { return !available_.empty(); })) {
        throw std::runtime_error("redis client pool acquire timeout");
    }

    auto client = std::move(available_.back());
    available_.pop_back();
    return Lease(this, std::move(client));
}

std::optional<RedisClientPool::Lease> RedisClientPool::TryAcquireFor(std::chrono::milliseconds timeout,
                                                                     std::string* error_message) {
    std::unique_lock lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this] { return !available_.empty(); })) {
        if (error_message != nullptr) {
            *error_message = "redis client pool acquire timeout";
        }
        return std::nullopt;
    }

    auto client = std::move(available_.back());
    available_.pop_back();
    return Lease(this, std::move(client));
}

void RedisClientPool::Return(std::unique_ptr<RedisClient> client) {
    {
        std::lock_guard lock(mutex_);
        available_.push_back(std::move(client));
    }
    cv_.notify_one();
}

}  // namespace common::redis
