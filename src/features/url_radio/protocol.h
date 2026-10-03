#pragma once
#include <string>
#include <algorithm>
#include <cctype>

namespace url_radio {
constexpr size_t kMaxUrl = 384;
constexpr size_t kMaxRequest = 480;

// The value is data, never an engine command or shell command.
inline bool valid_url(const std::string& url) {
    if (url.size() > kMaxUrl) return false;
    size_t start = url.compare(0, 8, "https://") == 0 ? 8 :
                   url.compare(0, 7, "http://") == 0 ? 7 : 0;
    if (!start || start >= url.size()) return false;
    for (unsigned char c : url) {
        if (c <= 32 || c >= 127 || c == '"' || c == '\\' || c == '|' || c == ';')
            return false;
    }
    const size_t end = url.find_first_of("/?#", start);
    const std::string authority = url.substr(start, end - start);
    if (authority.empty() || authority.find('@') != std::string::npos) return false;
    return authority[0] != ':';
}

struct Request { std::string nonce; bool play = false; std::string url; };
inline bool parse_request(const std::string& value, Request& request) {
    const std::string prefix = "SB_RADIO_V1|";
    if (value.size() > kMaxRequest || value.compare(0, prefix.size(), prefix) != 0)
        return false;
    const size_t split = value.find('|', prefix.size());
    if (split == std::string::npos) return false;
    const std::string nonce = value.substr(prefix.size(), split - prefix.size());
    if (nonce.empty() || nonce.size() > 20 ||
        !std::all_of(nonce.begin(), nonce.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
        return false;
    const std::string action = value.substr(split + 1);
    Request parsed;
    parsed.nonce = nonce;
    if (action == "stop") {
        parsed.play = false;
    } else if (action.compare(0, 5, "play|") == 0) {
        parsed.url = action.substr(5);
        if (!valid_url(parsed.url)) return false;
        parsed.play = true;
    } else return false;
    request = parsed;
    return true;
}
} // namespace url_radio
