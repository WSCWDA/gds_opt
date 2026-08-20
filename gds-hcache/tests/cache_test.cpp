#include "cache.hpp"

#include <cassert>
#include <chrono>
#include <thread>

int main() {
  using namespace ghc::detail;
  std::size_t evictions = 0;
  BlockCache cache(2, [&](std::size_t) { ++evictions; });

  auto first = cache.get_or_reserve({1, 0});
  assert(first.owner && !first.hit);
  cache.publish(first.line, 4096, 0);
  cache.release(first.line);

  auto hit = cache.get_or_reserve({1, 0});
  assert(!hit.owner && hit.hit && hit.line->valid_bytes == 4096);
  cache.release(hit.line);

  auto second = cache.get_or_reserve({1, 4096});
  cache.publish(second.line, 4096, 0);
  cache.release(second.line);
  auto third = cache.get_or_reserve({1, 8192});
  cache.publish(third.line, 4096, 0);
  assert(evictions == 1);

  BlockCache coalescing(2);
  auto filling = coalescing.get_or_reserve({2, 0});
  bool observed = false;
  std::thread waiter([&] {
    auto result = coalescing.get_or_reserve({2, 0});
    observed = result.hit && result.coalesced && !result.owner;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  coalescing.publish(filling.line, 2048, 0);
  coalescing.release(filling.line);
  waiter.join();
  assert(observed);
  return 0;
}
