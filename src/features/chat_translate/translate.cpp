#include "feature_config.h"
#if FEATURE_CHAT_TRANSLATE
#include "generated_detours.h"
#include "detours.h"
#include "sof_compat.h"
#include "util.h"
#include "backend.h"
#include "capture.h"
#include <windows.h>
#include <cmath>
#include <cstdlib>
#include <cstdarg>
#include <fstream>
#include <iterator>

void chat_translate_pump();
namespace {
using Print = void (__cdecl *)(const char*, ...);
Print original_print = nullptr;
chat_translate::LineBuffer lines;
unsigned seen = 0, frames = 0, pumps = 0, displayed = 0;
std::string last_language = "none", last_result = "none";
bool suppress_capture = false;
cvar_t *enabled = nullptr, *codepage = nullptr, *local_slot_cvar = nullptr, *debug_self = nullptr;
unsigned skipped_self = 0, skipped_unslotted = 0, skipped_unknown_local = 0;
bool ready = false, connected_before = false, enabled_before = false;
uint64_t generation = 1;
unsigned candidates = 0, queued = 0;
std::string key;
thread_local bool inside_print = false;

void print(const std::string& message) {
    auto target = original_print ? original_print : detour_Com_Printf::oCom_Printf;
    const bool previous = suppress_capture;
    suppress_capture = true;
    if (target) target("[translate] %s\n", message.c_str());
    suppress_capture = previous;
}
UINT input_codepage() {
    if (!codepage || !std::isfinite(codepage->value) || codepage->value < 0 || codepage->value > 65001) return 1252;
    const int cp = static_cast<int>(codepage->value);
    if ((cp >= 1250 && cp <= 1258) || cp == 437 || cp == 850 || cp == 866 || cp == 65001) return static_cast<UINT>(cp);
    return 1252;
}
std::string convert(const std::string& text, UINT from, UINT to, DWORD flags) {
    const int n = MultiByteToWideChar(from, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!n) return {};
    std::wstring wide(n, L'\0');
    if (!MultiByteToWideChar(from, flags, text.data(), static_cast<int>(text.size()), &wide[0], n)) return {};
    const int size = WideCharToMultiByte(to, 0, wide.data(), n, nullptr, 0, nullptr, nullptr);
    if (!size) return {};
    std::string result(size, '\0');
    if (!WideCharToMultiByte(to, 0, wide.data(), n, &result[0], size, nullptr, nullptr)) return {};
    return result;
}
std::string utf8(const std::string& text) {
    // Accept valid UTF-8 first; otherwise interpret the engine's legacy bytes.
    const auto valid = convert(text, CP_UTF8, CP_UTF8, MB_ERR_INVALID_CHARS);
    return valid.empty() ? convert(text, input_codepage(), CP_UTF8, 0) : valid;
}
bool active() {
    static int* state = static_cast<int*>(rvaToAbsExe(reinterpret_cast<void*>(0x001C1F00)));
    return state && *state == ca_active;
}
void sync_context() {
    if (!enabled) return;
    const bool on = enabled->value != 0, connected = active();
    if (on != enabled_before || connected != connected_before) {
        ++generation;
        lines.clear();
        chat_translate::configure(key, generation);
        if (on && !enabled_before) print("Enabled. Chat text is sent to DeepL; original messages remain visible.");
        enabled_before = on; connected_before = connected;
    }
}
int local_slot() {
    if (!local_slot_cvar || !local_slot_cvar->string || !local_slot_cvar->string[0]) return -1;
    char* end = nullptr;
    const long slot = std::strtol(local_slot_cvar->string, &end, 10);
    return end && !*end && slot >= 0 && slot <= 31 ? static_cast<int>(slot) : -1;
}
void observe_line(const std::string& message) {
    sync_context();
    if (!ready || !enabled || enabled->value == 0 || key.empty() || !active()) return;
    chat_translate::Chat chat;
    if (!chat_translate::parse_chat(message, chat)) return;
    ++candidates;
    const int local = local_slot();
    const bool include_self = debug_self && debug_self->value != 0;
    if (!chat_translate::eligible_slot(chat, local, include_self)) {
        if (chat.slot < 0) ++skipped_unslotted;
        else if (local < 0) ++skipped_unknown_local;
        else ++skipped_self;
        return;
    }
    const auto text = utf8(chat.text);
    if (!text.empty() && chat_translate::submit(chat.speaker, text, generation)) ++queued;
}
void observe(const std::string& chunk) {
    if (suppress_capture || !ready) return;
    ++seen;
    for (const auto& line : lines.append(chunk)) observe_line(line);
}
// The generator intentionally keeps Com_Printf pointer-only because forwarding
// an unknown variadic argument list as named arguments would corrupt printing.
// This manual detour formats the complete va_list, then forwards using "%s".
void __cdecl hooked_print(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const auto message = chat_translate::format_print(fmt, args);
    va_end(args);
    if (!original_print) return;
    if (inside_print) { original_print("%s", message.c_str()); return; }
    inside_print = true;
    original_print("%s", message.c_str());
    try { observe(message); } catch (...) { /* Never break engine printing. */ }
    inside_print = false;
}
void reload() {
    key.clear();
    // Fixed local file; never a cvar, console argument, userinfo or server data.
    std::ifstream input("sof_buddy/deepl.key", std::ios::binary);
    std::string candidate;
    char c;
    while (input.get(c) && candidate.size() <= 512) candidate.push_back(c);
    while (!candidate.empty() && (candidate.back() == '\r' || candidate.back() == '\n' || candidate.back() == ' ' || candidate.back() == '\t')) candidate.pop_back();
    const auto first = candidate.find_first_not_of(" \t\r\n");
    if (first != std::string::npos) candidate.erase(0, first);
    if (chat_translate::valid_key(candidate)) key = candidate;
    ++generation;
    chat_translate::configure(key, generation, true);
    print(key.empty() ? "Missing or invalid sof_buddy/deepl.key. Put your DeepL API Free key in that file, then reload."
                      : "DeepL API Free key loaded. Translation sends chat text to DeepL when enabled.");
}
void cmd_status() {
    chat_translate_pump();
    const auto s = chat_translate::stats();
    print(std::string(enabled && enabled->value != 0 ? "enabled" : "disabled") +
          "; key=" + (key.empty() ? "missing" : "loaded") + "; chat_hook=" + (original_print ? "installed" : "failed") +
          "; capture=printf" + "; seen=" + std::to_string(seen) +
          "; cls=" + std::to_string(*static_cast<int*>(rvaToAbsExe(reinterpret_cast<void*>(0x001C1F00)))) +
          "; local_slot=" + std::to_string(local_slot()) + "; debug_self=" + (debug_self && debug_self->value != 0 ? "on" : "off") +
          "; skipped_self=" + std::to_string(skipped_self) + "; skipped_unslotted=" + std::to_string(skipped_unslotted) + "; skipped_unknown_local=" + std::to_string(skipped_unknown_local) +
          "; frames=" + std::to_string(frames) + "; pumps=" + std::to_string(pumps) +
          "; displayed=" + std::to_string(displayed) + "; last_source=" + last_language + "; last_result=" + last_result +
          "; matched=" + std::to_string(candidates) + "; queued=" + std::to_string(queued) +
          "; API requests=" + std::to_string(s.requests) + "; cache_hits=" + std::to_string(s.cache_hits) +
          "; dropped=" + std::to_string(s.dropped) + "; quota/key pause=" + (s.blocked ? "yes" : "no"));
}
void cmd_test() {
    sync_context();
    if (detour_Cmd_Argc::oCmd_Argc() != 2) { print("Usage: sofbuddy_translate_test \"Bonjour, bonne partie!\""); return; }
    if (!enabled || enabled->value == 0 || key.empty()) { print("Enable translation and load a valid key first."); return; }
    const std::string text = detour_Cmd_Argv::oCmd_Argv(1);
    if (text.empty() || text.size() > 512) { print("Test text must be 1..512 bytes."); return; }
    const auto converted = utf8(text);
    if (converted.empty() || !chat_translate::submit("Test", converted, generation)) print("Test not queued: paused or queue full.");
    else print("Test queued. Its API response will be displayed; use status after 20 seconds if needed.");
}
}

void chat_translate_PostCvarInit() {
    enabled = detour_Cvar_Get::oCvar_Get("_sofbuddy_translate_enabled", "0", CVAR_SOFBUDDY_ARCHIVE, nullptr);
    local_slot_cvar = detour_Cvar_Get::oCvar_Get("_sp_cl_info_slot", "-1", 0, nullptr);
    debug_self = detour_Cvar_Get::oCvar_Get("_sofbuddy_translate_debug_self", "0", CVAR_SOFBUDDY_ARCHIVE, nullptr);
    codepage = detour_Cvar_Get::oCvar_Get("_sofbuddy_translate_codepage", "1252", CVAR_SOFBUDDY_ARCHIVE, nullptr);
    detour_Cmd_AddCommand::oCmd_AddCommand(const_cast<char*>("sofbuddy_translate_reload"), reload);
    detour_Cmd_AddCommand::oCmd_AddCommand(const_cast<char*>("sofbuddy_translate_status"), cmd_status);
    detour_Cmd_AddCommand::oCmd_AddCommand(const_cast<char*>("sofbuddy_translate_test"), cmd_test);
    void* target = reinterpret_cast<void*>(detour_Com_Printf::oCom_Printf);
    // Known upstream Com_Printf entry; let the existing detour system relocate
    // the prologue. This pointer-only entry has no generated competing detour.
    if (target && DetourSystem::Instance().ApplyDetourAtAddress(target, reinterpret_cast<void*>(hooked_print),
            reinterpret_cast<void**>(&original_print), "ChatTranslate_Com_Printf", 0) && original_print) {
        ready = true;
    } else print("Could not install chat capture hook. Translation test command remains available.");
    ready = original_print != nullptr;
    print("Capture v6: working printf capture restored; strict player slots, self excluded by default.");
    reload();
}
void chat_translate_pump() {
    if (!enabled) return;
    ++pumps;
    sync_context();
    for (const auto& line : lines.flush()) observe_line(line);
    const bool on = enabled->value != 0;
    for (const auto& result : chat_translate::results()) {
        last_language = result.language.empty() ? "none" : result.language;
        if (!on || result.generation != generation) { last_result = "discarded_stale_or_disabled"; continue; }
        if (!result.error.empty()) { last_result = "API_error"; print(result.error); continue; }
        // Explicit tests always display the response, even if detection says EN.
        if (result.speaker != "Test" && (result.language == "EN" || result.language == "EN-GB" || result.language == "EN-US")) {
            last_result = "English_suppressed"; continue;
        }
        const auto text = convert(result.text, CP_UTF8, 1252, MB_ERR_INVALID_CHARS);
        if (text.empty()) { last_result = "encoding_error"; print("Translation encoding could not be displayed."); continue; }
        ++displayed;
        last_result = "displayed";
        print(chat_translate::safe_display(result.speaker) + " [" + result.language + " -> EN]: " + chat_translate::safe_display(text));
    }
}
void chat_translate_frame_post_callback(int msec) {
    (void)msec;
    ++frames;
    chat_translate_pump();
}
void chat_translate_shutdown_pre_callback() {
    ready = false;
    chat_translate::shutdown();
    key.clear();
}
#endif
