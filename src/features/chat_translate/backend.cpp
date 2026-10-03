#include "feature_config.h"
#if FEATURE_CHAT_TRANSLATE
#include "backend.h"
#include "transport.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace chat_translate {
namespace {
using Clock = std::chrono::steady_clock;
struct Job { std::string speaker, text; uint64_t generation; };
struct State {
    std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;
    std::deque<Job> jobs;
    std::deque<Result> completed;
    std::string key;
    uint64_t generation = 0, credentials = 0;
    bool exit = false;
    Stats stats;
};
// CL_Shutdown joins explicitly, never under the DLL loader lock.
State& shared() { static State* s = new State; return *s; }
void run() {
    auto& s = shared();
    std::map<std::string, Translation> cache;
    uint64_t credentials = 0;
    auto next = Clock::time_point::min();
    for (;;) {
        Job job;
        std::string key;
        uint64_t revision;
        {
            std::unique_lock<std::mutex> lock(s.mutex);
            s.changed.wait(lock, [&] { return s.exit || (!s.jobs.empty() && !s.stats.blocked); });
            if (s.exit) return;
            if (Clock::now() < next && !(s.credentials == credentials && cache.find(s.jobs.front().text) != cache.end())) {
                s.changed.wait_until(lock, next, [&] { return s.exit || s.credentials != credentials; });
                if (s.exit) return;
                if (s.credentials != credentials) next = Clock::time_point::min();
            }
            if (s.jobs.empty() || s.stats.blocked) continue;
            job = s.jobs.front(); s.jobs.pop_front();
            if (job.generation != s.generation || s.key.empty()) continue;
            key = s.key;
            revision = s.credentials;
            if (revision != credentials) { cache.clear(); credentials = revision; }
        }
        Result out;
        out.generation = job.generation;
        out.speaker = job.speaker;
        auto found = cache.find(job.text);
        if (found != cache.end()) {
            out.text = found->second.text; out.language = found->second.language;
            std::lock_guard<std::mutex> lock(s.mutex);
            ++s.stats.cache_hits;
        } else {
            {
                std::lock_guard<std::mutex> lock(s.mutex);
                if (job.generation != s.generation || revision != s.credentials || s.exit) continue;
                ++s.stats.requests;
            }
            const auto reply = translate_request(key, job.text);
            next = Clock::now() + std::chrono::milliseconds(250);
            if (reply.valid) {
                if (cache.size() >= 256) cache.erase(cache.begin());
                cache[job.text] = reply.translation;
                out.text = reply.translation.text; out.language = reply.translation.language;
            } else {
                if (reply.status == 456) out.error = "DeepL quota exhausted. Requests paused; reload after quota renewal.";
                else if (reply.status == 401 || reply.status == 403) out.error = "DeepL rejected the key. Check deepl.key and run sofbuddy_translate_reload.";
                else if (reply.status == 429) out.error = "DeepL rate limit. Pausing requests for 30 seconds.";
                else if (reply.status == 0) out.error = "DeepL connection failed. Check Internet access; retrying later.";
                else out.error = "DeepL request failed (HTTP " + std::to_string(reply.status) + ").";
                next = Clock::now() + std::chrono::seconds(30);
                if (reply.status == 456 || reply.status == 401 || reply.status == 403) {
                    std::lock_guard<std::mutex> lock(s.mutex);
                    if (revision == s.credentials) { s.stats.blocked = true; s.jobs.clear(); }
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            if (job.generation != s.generation || revision != s.credentials || s.exit) continue;
            if (s.completed.size() >= 16) s.completed.pop_front();
            s.completed.push_back(std::move(out));
        }
    }
}
}
void configure(const std::string& key, uint64_t generation, bool reset_credentials) {
    auto& s = shared();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (key != s.key || reset_credentials) { ++s.credentials; s.stats.blocked = false; }
    s.key = key; s.generation = generation;
    s.jobs.clear(); s.completed.clear();
    s.changed.notify_all();
}
bool submit(const std::string& speaker, const std::string& utf8, uint64_t generation) {
    auto& s = shared();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.exit || s.stats.blocked || s.key.empty() || generation != s.generation) return false;
    if (s.jobs.size() >= 8) { ++s.stats.dropped; return false; }
    // Avoid duplicate in-flight queue entries from repeated console prints.
    for (const auto& job : s.jobs) if (job.speaker == speaker && job.text == utf8) return false;
    s.jobs.push_back({speaker, utf8, generation});
    if (!s.worker.joinable()) s.worker = std::thread(run);
    s.changed.notify_all();
    return true;
}
std::vector<Result> results() {
    auto& s = shared();
    std::lock_guard<std::mutex> lock(s.mutex);
    std::vector<Result> out(s.completed.begin(), s.completed.end());
    s.completed.clear();
    return out;
}
Stats stats() { auto& s = shared(); std::lock_guard<std::mutex> lock(s.mutex); return s.stats; }
void shutdown() {
    auto& s = shared();
    { std::lock_guard<std::mutex> lock(s.mutex); s.exit = true; s.jobs.clear(); s.changed.notify_all(); }
    if (s.worker.joinable()) s.worker.join();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.key.clear(); s.completed.clear();
}
}
#endif
