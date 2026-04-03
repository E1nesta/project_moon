#pragma once

#include "runtime/storage/mysql/mysql_client.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace common::mysql {

class MySqlClientPool {
public:
    class Lease {
    public:
        Lease() = default;
        Lease(MySqlClientPool* pool, std::unique_ptr<MySqlClient> client);
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;
        ~Lease();

        MySqlClient& operator*() const;
        MySqlClient* operator->() const;
        [[nodiscard]] bool HasValue() const;

    private:
        void Reset();

        MySqlClientPool* pool_ = nullptr;
        std::unique_ptr<MySqlClient> client_;
    };

    MySqlClientPool(ConnectionOptions options, std::size_t pool_size);

    bool Initialize(std::string* error_message = nullptr);
    Lease Acquire();
    std::optional<Lease> TryAcquireFor(std::chrono::milliseconds timeout, std::string* error_message = nullptr);
    void Return(std::unique_ptr<MySqlClient> client);

private:
    ConnectionOptions options_;
    std::size_t pool_size_ = 1;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::unique_ptr<MySqlClient>> available_;
};

}  // namespace common::mysql
