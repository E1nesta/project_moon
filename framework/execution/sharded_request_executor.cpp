#include "framework/execution/sharded_request_executor.h"

#include <algorithm>
#include <utility>

namespace framework::execution {

namespace {

std::size_t NormalizeCount(std::size_t value) {
    return value == 0 ? std::size_t{1} : value;
}

}  // namespace

ShardedRequestExecutor::ShardedRequestExecutor(Options options) : options_(std::move(options)) {
    options_.worker_threads = NormalizeCount(options_.worker_threads);
    options_.shard_count = NormalizeCount(options_.shard_count);
}

ShardedRequestExecutor::~ShardedRequestExecutor() {
    Shutdown();
}

bool ShardedRequestExecutor::Start(std::string* error_message) {
    if (started_.exchange(true)) {
        return true;
    }

    if (options_.worker_threads != options_.shard_count) {
        if (error_message != nullptr) {
            *error_message = "execution.shard_count must equal execution.worker_threads";
        }
        started_.store(false);
        return false;
    }

    try {
        shards_.reserve(options_.shard_count);
        for (std::size_t index = 0; index < options_.shard_count; ++index) {
            auto shard = std::make_unique<Shard>();
            shard->worker = std::thread([this, raw = shard.get()] {
                RunShard(raw);
            });
            shards_.push_back(std::move(shard));
        }
        return true;
    } catch (const std::exception& exception) {
        if (error_message != nullptr) {
            *error_message = exception.what();
        }
        Shutdown();
        return false;
    }
}

bool ShardedRequestExecutor::Submit(const ExecutionKey& key,
                                    Task task,
                                    std::size_t* shard_index,
                                    std::string* error_message) {
    if (!started_.load()) {
        if (error_message != nullptr) {
            *error_message = "request executor not started";
        }
        return false;
    }

    if (!accepting_.load()) {
        if (error_message != nullptr) {
            *error_message = "request executor is stopping";
        }
        return false;
    }

    if (!TryReserveQueueSlot(error_message)) {
        return false;
    }

    const auto index = ResolveShard(key);
    if (shard_index != nullptr) {
        *shard_index = index;
    }

    {
        std::lock_guard lock(shards_[index]->mutex);
        shards_[index]->tasks.push_back(std::move(task));
    }
    shards_[index]->cv.notify_one();
    return true;
}

std::optional<std::size_t> ShardedRequestExecutor::PreviewShard(const ExecutionKey& key) const {
    if (!started_.load() || shards_.empty()) {
        return std::nullopt;
    }
    return ResolveShard(key);
}

void ShardedRequestExecutor::StopAccepting() {
    accepting_.store(false);
}

bool ShardedRequestExecutor::WaitForDrain(std::chrono::milliseconds timeout) {
    std::unique_lock lock(drain_mutex_);
    return drain_cv_.wait_for(lock, timeout, [this] {
        return pending_tasks_.load() == 0;
    });
}

void ShardedRequestExecutor::Shutdown() {
    accepting_.store(false);

    for (auto& shard : shards_) {
        {
            std::lock_guard lock(shard->mutex);
            shard->stop = true;
        }
        shard->cv.notify_all();
    }

    for (auto& shard : shards_) {
        if (shard->worker.joinable()) {
            shard->worker.join();
        }
    }
    shards_.clear();
    started_.store(false);
}

void ShardedRequestExecutor::RunShard(Shard* shard) {
    while (true) {
        Task task;
        {
            std::unique_lock lock(shard->mutex);
            shard->cv.wait(lock, [shard] {
                return shard->stop || !shard->tasks.empty();
            });
            if (shard->stop && shard->tasks.empty()) {
                return;
            }

            task = std::move(shard->tasks.front());
            shard->tasks.pop_front();
        }

        try {
            task();
        } catch (...) {
        }
        ReleaseQueueSlot();
    }
}

std::size_t ShardedRequestExecutor::ResolveShard(const ExecutionKey& key) const {
    return std::hash<std::string>{}(DescribeExecutionKey(key)) % std::max<std::size_t>(1, options_.shard_count);
}

bool ShardedRequestExecutor::TryReserveQueueSlot(std::string* error_message) {
    auto current = pending_tasks_.load();
    while (true) {
        if (current >= options_.queue_limit) {
            if (error_message != nullptr) {
                *error_message = "request executor queue limit exceeded";
            }
            return false;
        }
        if (pending_tasks_.compare_exchange_weak(current, current + 1)) {
            return true;
        }
    }
}

void ShardedRequestExecutor::ReleaseQueueSlot() {
    const auto remaining = pending_tasks_.fetch_sub(1) - 1;
    if (remaining == 0) {
        std::lock_guard lock(drain_mutex_);
        drain_cv_.notify_all();
    }
}

}  // namespace framework::execution
