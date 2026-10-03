#include "features/chat_translate/backend.h"
#include "features/chat_translate/transport.h"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
using namespace chat_translate;
using Clock = std::chrono::steady_clock;
namespace {
std::mutex mutex;
std::condition_variable changed;
bool release_first = false;
std::vector<Clock::time_point> starts;
}
namespace chat_translate {
TransportReply translate_request(const std::string& key, const std::string& text) {
    assert(key == "00000000-0000-0000-0000-000000000000:fx");
    {
        std::unique_lock<std::mutex> lock(mutex);
        starts.push_back(Clock::now());
        changed.notify_all();
        if (text == "old") changed.wait(lock, [] { return release_first; });
    }
    TransportReply out;
    out.status = text == "quota" ? 456 : 200;
    out.valid = out.status == 200;
    out.translation.language = text == "english" ? "EN" : "FR";
    out.translation.text = "Translated " + text;
    return out;
}
}
template<class Predicate> void await(Predicate predicate) {
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        assert(Clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
int main() {
    const std::string key = "00000000-0000-0000-0000-000000000000:fx";
    configure(key, 1);
    assert(submit("Old", "old", 1));
    await([] { std::lock_guard<std::mutex> lock(mutex); return starts.size() == 1; });
    for (int i = 0; i < 8; ++i) assert(submit("Old", "pending" + std::to_string(i), 1));
    assert(!submit("Old", "overflow", 1));
    assert(stats().dropped == 1);
    configure(key, 2); // disconnect/disable discards old queued and in-flight work
    assert(!submit("Old", "wrong generation", 1));
    assert(submit("New", "bonjour", 2));
    { std::lock_guard<std::mutex> lock(mutex); release_first = true; changed.notify_all(); }
    std::vector<Result> output;
    await([&] { output = results(); return !output.empty(); });
    assert(output.size() == 1 && output[0].speaker == "New" && output[0].generation == 2);
    assert(output[0].text == "Translated bonjour" && stats().requests == 2);
    { std::lock_guard<std::mutex> lock(mutex); assert(starts[1] - starts[0] >= std::chrono::milliseconds(200)); }
    const auto cache_started = Clock::now();
    assert(submit("Cached", "bonjour", 2));
    await([&] { output = results(); return !output.empty(); });
    assert(Clock::now() - cache_started < std::chrono::milliseconds(200));
    assert(output[0].speaker == "Cached" && stats().cache_hits == 1 && stats().requests == 2);
    assert(submit("Quota", "quota", 2));
    await([&] { output = results(); return !output.empty(); });
    assert(!output[0].error.empty() && stats().blocked);
    assert(!submit("Blocked", "bonjour", 2));
    configure(key, 3); // changing map/enable state must not bypass quota pause
    assert(stats().blocked && !submit("Blocked", "bonjour", 3));
    configure(key, 4, true); // explicit credential reload resets pause and cache
    assert(!stats().blocked && submit("English", "english", 4));
    await([&] { output = results(); return !output.empty(); });
    assert(output[0].language == "EN" && output[0].error.empty());
    shutdown();
    assert(!submit("After quit", "hello", 4));
    assert(stats().requests == 4);
    std::cout << "Background queue bounds, rate limiting, stale-work cancellation, cache and quota-pause tests passed.\n";
}
