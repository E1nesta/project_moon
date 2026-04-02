#include "common/mysql/mysql_client_pool.h"

#include <stdexcept>
#include <utility>

namespace common::mysql {

MySqlClientPool::Lease::Lease(MySqlClientPool* pool, std::unique_ptr<MySqlClient> client)
    : pool_(pool), client_(std::move(client)) {}

MySqlClientPool::Lease::Lease(Lease&& other) noexcept
    : pool_(other.pool_), client_(std::move(other.client_)) {
    other.pool_ = nullptr;
}

MySqlClientPool::Lease& MySqlClientPool::Lease::operator=(Lease&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    Reset();
    pool_ = other.pool_;
    client_ = std::move(other.client_);
    other.pool_ = nullptr;
    return *this;
}

MySqlClientPool::Lease::~Lease() {
    Reset();
}

MySqlClient& MySqlClientPool::Lease::operator*() const {
    return *client_;
}

MySqlClient* MySqlClientPool::Lease::operator->() const {
    return client_.get();
}

bool MySqlClientPool::Lease::HasValue() const {
    return client_ != nullptr;
}

void MySqlClientPool::Lease::Reset() {
    if (pool_ != nullptr && client_ != nullptr) {
        pool_->Return(std::move(client_));
    }
    pool_ = nullptr;
}

MySqlClientPool::MySqlClientPool(ConnectionOptions options, std::size_t pool_size)
    : options_(std::move(options)), pool_size_(pool_size == 0 ? std::size_t{1} : pool_size) {}

bool MySqlClientPool::Initialize(std::string* error_message) {
    std::lock_guard lock(mutex_);
    if (!available_.empty()) {
        return true;
    }

    available_.reserve(pool_size_);
    for (std::size_t index = 0; index < pool_size_; ++index) {
        auto client = std::make_unique<MySqlClient>(options_);
        if (!client->Ping(error_message)) {
            return false;
        }
        available_.push_back(std::move(client));
    }
    return true;
}

MySqlClientPool::Lease MySqlClientPool::Acquire() {
    std::unique_lock lock(mutex_);
    if (!cv_.wait_for(lock, std::chrono::seconds(5), [this] { return !available_.empty(); })) {
        throw std::runtime_error("mysql client pool acquire timeout");
    }

    auto client = std::move(available_.back());
    available_.pop_back();
    return Lease(this, std::move(client));
}

std::optional<MySqlClientPool::Lease> MySqlClientPool::TryAcquireFor(std::chrono::milliseconds timeout,
                                                                     std::string* error_message) {
    std::unique_lock lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this] { return !available_.empty(); })) {
        if (error_message != nullptr) {
            *error_message = "mysql client pool acquire timeout";
        }
        return std::nullopt;
    }

    auto client = std::move(available_.back());
    available_.pop_back();
    return Lease(this, std::move(client));
}

void MySqlClientPool::Return(std::unique_ptr<MySqlClient> client) {
    {
        std::lock_guard lock(mutex_);
        available_.push_back(std::move(client));
    }
    cv_.notify_one();
}

}  // namespace common::mysql
