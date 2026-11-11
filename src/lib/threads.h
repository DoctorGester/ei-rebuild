#pragma once

// Work spread over the processors: the PNG reader's segments, the textures made from PNGs together
// (docs/notes/mods.md, "Textures as PNG", "Loading cost").

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace lib {

// `work(i)` for i in [0, count), on up to one thread a processor (the calling one among them); each i on one thread.
template <class Work>
void OnThreads(int count, Work work) {
    const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, std::max(count, 1));
    std::atomic<int> next{0};
    const auto run = [&] {
        for (int i = next++; i < count; i = next++) work(i);
    };
    std::vector<std::thread> helpers;
    for (int t = 1; t < threads; ++t) helpers.emplace_back(run);
    run();
    for (std::thread& helper : helpers) helper.join();
}

} // namespace lib
