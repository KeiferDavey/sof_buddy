#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace chat_translate {
// Match a direct call to the known console writer in the original Com_Printf
// body, followed by a single-argument stack cleanup. Never hook on RVA alone.
inline bool console_call_verified(const unsigned char* code, size_t size, uintptr_t start, uintptr_t target) {
    if (!code || size < 8) return false;
    for (size_t i = 0; i + 8 <= size; ++i) {
        if (code[i] != 0xe8) continue;
        const uint32_t bits = uint32_t(code[i+1]) | uint32_t(code[i+2]) << 8 |
                              uint32_t(code[i+3]) << 16 | uint32_t(code[i+4]) << 24;
        const uintptr_t callee = start + i + 5 + static_cast<int32_t>(bits);
        if (callee != target) continue;
        if (code[i+5] == 0x83 && code[i+6] == 0xc4 && code[i+7] == 4) return true;
        // Verified in the supplied SoF.exe at 0x2001C795:
        // call Con_Print; mov edx,[absolute]; add esp,4.
        if (i + 14 <= size && code[i+5] == 0x8b && code[i+6] == 0x15 &&
            code[i+11] == 0x83 && code[i+12] == 0xc4 && code[i+13] == 4) return true;
    }
    return false;
}
class LineBuffer {
    std::string pending;
    bool overflow = false;
public:
    void clear() { pending.clear(); overflow = false; }
    std::vector<std::string> append(const std::string& chunk) {
        std::vector<std::string> lines;
        for (char c : chunk) {
            if (c == '\n' || c == '\r') {
                if (!overflow && !pending.empty()) lines.push_back(pending);
                clear();
            } else if (!overflow) {
                if (pending.size() >= 1024) { pending.clear(); overflow = true; }
                else pending.push_back(c);
            }
        }
        return lines;
    }
    std::vector<std::string> flush() {
        std::vector<std::string> lines;
        if (!overflow && !pending.empty()) lines.push_back(pending);
        clear();
        return lines;
    }
};
}
