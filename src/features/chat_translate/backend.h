#pragma once
#include "protocol.h"
#include <cstdint>
#include <string>
#include <vector>
namespace chat_translate {
struct Result {
    uint64_t generation = 0;
    std::string speaker, text, language, error;
};
struct Stats { unsigned requests = 0, cache_hits = 0, dropped = 0; bool blocked = false; };
void configure(const std::string& key, uint64_t generation, bool reset_credentials = false);
bool submit(const std::string& speaker, const std::string& utf8, uint64_t generation);
std::vector<Result> results();
Stats stats();
void shutdown();
}
