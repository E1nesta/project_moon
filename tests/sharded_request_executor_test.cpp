#include "runtime/execution/sharded_request_executor.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

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
    framework::execution::ShardedRequestExecutor executor({1, 1, 4});
    std::string error_message;
    if (!Expect(executor.Start(&error_message), "expected executor to start: " + error_message)) {
        return 1;
    }

    const framework::execution::ExecutionKey key{
        framework::execution::ExecutionKeyKind::kPlayer, "20001"};

    std::atomic_bool first_task_started{false};
    std::atomic_bool second_task_finished{false};

    if (!Expect(executor.Submit(key, [&first_task_started] {
                    first_task_started.store(true);
                    throw std::runtime_error("boom");
                }),
                "expected first task submission to succeed")) {
        executor.Shutdown();
        return 1;
    }

    if (!Expect(executor.Submit(key, [&second_task_finished] {
                    second_task_finished.store(true);
                }),
                "expected second task submission to succeed")) {
        executor.Shutdown();
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while ((!first_task_started.load() || !second_task_finished.load()) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!Expect(first_task_started.load(), "expected first task to run")) {
        executor.Shutdown();
        return 1;
    }

    if (!Expect(second_task_finished.load(), "expected executor to keep processing after exception")) {
        executor.Shutdown();
        return 1;
    }

    if (!Expect(executor.WaitForDrain(std::chrono::seconds(1)), "expected executor to drain")) {
        executor.Shutdown();
        return 1;
    }

    executor.Shutdown();
    return 0;
}
