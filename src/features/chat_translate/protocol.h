#pragma once
#include "vendor/picojson.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace chat_translate {
struct Chat { std::string speaker, text; int slot = -1; };
// Deliberately conservative heuristic, not an engine PRINT_CHAT discriminator.
inline bool parse_chat(std::string line, Chat& chat) {
    chat.slot = -1;
    if (line.size() > 1024) return false;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    if (line.empty() || line.find_first_of("\r\n") != std::string::npos) return false;
    // SoFplus embeds palette/color controls throughout player names and text.
    line.erase(std::remove_if(line.begin(), line.end(), [](unsigned char c) {
        return (c < 32 && c != '\t') || c == 127;
    }), line.end());
    if (line.empty() || line.compare(0, 12, "[translate] ") == 0) return false;
    const auto colon = line.find(": ");
    if (colon == std::string::npos || colon == 0 || colon > 64) return false;
    chat.speaker = line.substr(0, colon);
    while (!chat.speaker.empty() && (chat.speaker.back() == ' ' || chat.speaker.back() == '\t')) chat.speaker.pop_back();
    chat.text = line.substr(colon + 2);
    // SoFplus slot marker, as seen in the supplied console dump: Name : [0] text
    if (!chat.text.empty() && chat.text[0] == '[') {
        const auto close = chat.text.find("] ");
        if (close == 2 || close == 3) {
            int slot = 0; bool numeric = true;
            for (size_t i = 1; i < close; ++i) {
                if (chat.text[i] < '0' || chat.text[i] > '9') { numeric = false; break; }
                slot = slot * 10 + chat.text[i] - '0';
            }
            if (numeric && slot <= 31) { chat.slot = slot; chat.text.erase(0, close + 2); }
        }
    }
    if (chat.speaker.empty() || chat.text.empty() || chat.text.size() > 512 || chat.speaker.find(":") != std::string::npos) return false;
    for (unsigned char c : chat.speaker) if (c < 32) return false;
    // SoFplus diagnostics resemble chat but have no player message body.
    if (chat.speaker.compare(0, 8, "Client [") == 0 || chat.speaker.compare(0, 10, "Incoming [") == 0) return false;
    // Common English greetings/slang are too short for reliable API detection.
    std::string token;
    for (unsigned char c : chat.text) {
        if (c >= 'A' && c <= 'Z') token.push_back(static_cast<char>(c + ('a' - 'A')));
        else if ((c >= 'a' && c <= 'z') || c >= 128) token.push_back(static_cast<char>(c));
        else if (c != ' ' && c != '\t' && c != '!' && c != '.' && c != '?' && c != ',') token.push_back(static_cast<char>(c));
    }
    const char* english_chat[] = {"hi", "hello", "hey", "lol", "lmao", "rofl", "ok", "okay", "gg", "gl", "hf", "thx", "ty", "np", "brb", "afk"};
    for (const char* word : english_chat) if (token == word) return false;
    // Console echoes and URLs are not chat. Common server diagnostics excluded.
    const char* excluded[] = {"http", "https", "Usage", "ERROR", "Error", "WARNING", "Warning",
                             "Connecting to", "Server", "server", "Client", "client", "Downloading"};
    for (const char* name : excluded) if (chat.speaker == name) return false;
    bool letter = false;
    for (unsigned char c : chat.text) if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 128) letter = true;
    return letter;
}
inline bool eligible_slot(const Chat& chat, int local, bool debug_self) {
    if (chat.slot < 0 || chat.slot > 31) return false;
    if (debug_self) return true;
    return local >= 0 && local <= 31 && chat.slot != local;
}
inline std::string request_body(const std::string& utf8) {
    picojson::object body;
    picojson::array texts;
    texts.emplace_back(utf8);
    body["text"] = picojson::value(texts);
    body["target_lang"] = picojson::value(std::string("EN-GB"));
    return picojson::value(body).serialize();
}
struct Translation { std::string language, text; };
inline bool parse_response(const std::string& body, Translation& result) {
    if (body.size() > 65536) return false;
    picojson::value value;
    if (!picojson::parse(value, body).empty() || !value.is<picojson::object>()) return false;
    const auto& translations = value.get("translations");
    if (!translations.is<picojson::array>() || translations.get<picojson::array>().size() != 1) return false;
    const auto& item = translations.get<picojson::array>()[0];
    if (!item.is<picojson::object>() || !item.get("text").is<std::string>() ||
        !item.get("detected_source_language").is<std::string>()) return false;
    result.text = item.get("text").get<std::string>();
    result.language = item.get("detected_source_language").get<std::string>();
    if (result.text.empty() || result.text.size() > 4096 || result.language.empty() || result.language.size() > 12) return false;
    for (unsigned char c : result.language) if (!(c >= 'A' && c <= 'Z') && c != '-') return false;
    return true;
}
inline std::string safe_display(std::string value) {
    for (char& c : value) if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
    if (value.size() > 768) value = value.substr(0, 765) + "...";
    return value;
}
inline bool valid_key(const std::string& key) {
    if (key.size() < 16 || key.size() > 256) return false;
    for (unsigned char c : key) if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
        !(c >= '0' && c <= '9') && c != '-' && c != ':' && c != '_') return false;
    return true;
}
inline std::string format_print(const char* fmt, va_list args) {
    if (!fmt) return {};
    va_list count;
    va_copy(count, args);
    const int n = std::vsnprintf(nullptr, 0, fmt, count);
    va_end(count);
    if (n < 0) return {};
    std::vector<char> buffer(static_cast<size_t>(n) + 1);
    std::vsnprintf(buffer.data(), buffer.size(), fmt, args);
    return std::string(buffer.data());
}
} // namespace chat_translate
