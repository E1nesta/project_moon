#pragma once

#include "runtime/execution/execution_types.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace framework::execution {

enum class SubmitFailureCode {
    kNone,
    kExecutionKeyUnresolved,
    kNotStarted,
    kStopping,
    kQueueLimitExceeded,
};

// Runs business tasks on fixed shards.
// Transport threads hand work off here; the same execution key is always routed to the same shard.
class ShardedRequestExecutor {
public:
    struct Options {
        std::size_t worker_threads = 1;
        std::size_t shard_count = 1;
        std::size_t queue_limit = 1024;
    };

    using Task = std::function<void()>;

    explicit ShardedRequestExecutor(Options options);
    ~ShardedRequestExecutor();

    ShardedRequestExecutor(const ShardedRequestExecutor&) = delete;
    ShardedRequestExecutor& operator=(const ShardedRequestExecutor&) = delete;

    bool Start(std::string* error_message = nullptr);
    bool Submit(const ExecutionKey& key,
                Task task,
                std::size_t* shard_index = nullptr,
                std::string* error_message = nullptr,
                SubmitFailureCode* failure_code = nullptr);
    [[nodiscard]] std::optional<std::size_t> PreviewShard(const ExecutionKey& key) const;
    // Reject new submissions while letting already queued work continue to drain.
    void StopAccepting();
    // Wait until all queued and running tasks finish, or until the grace timeout expires.
    [[nodiscard]] bool WaitForDrain(std::chrono::milliseconds timeout);
    // Stop workers after drain or timeout handling has been decided by the caller.
    void Shutdown();

private:
    struct Shard {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<Task> tasks;
        std::thread worker;
        bool stop = false;
    };

    void RunShard(Shard* shard);
    [[nodiscard]] std::size_t ResolveShard(const ExecutionKey& key) const;
    bool TryReserveQueueSlot(std::string* error_message, SubmitFailureCode* failure_code);
    void ReleaseQueueSlot();

    Options options_;
    std::vector<std::unique_ptr<Shard>> shards_;
    std::atomic_bool accepting_{true};
    std::atomic_bool started_{false};
    std::atomic<std::size_t> pending_tasks_{0};
    mutable std::mutex drain_mutex_;
    std::condition_variable drain_cv_;
};

}  // namespace framework::execution
