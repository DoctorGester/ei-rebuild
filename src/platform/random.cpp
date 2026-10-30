#include "platform.h"

#include <random>

namespace platform {
void RandomWords(std::span<std::uint32_t> words) {
    std::random_device random;
    for (auto& word : words) word = random();
}
}
