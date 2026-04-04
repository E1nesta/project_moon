#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace common::id {

class IdGenerator {
public:
    explicit IdGenerator(std::uint16_t node_id = 1);

    [[nodiscard]] std::int64_t Next();
    [[nodiscard]] std::string NextString();

private:
    std::uint16_t node_id_ = 1;
    std::atomic<std::uint64_t> sequence_{0};
};

}  // namespace common::id
