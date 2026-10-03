#include "feature_config.h"
#if FEATURE_URL_RADIO
#include "backend.h"
#include <windows.h>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <deque>
#include <chrono>
#include <cstdio>
#include <cstring>

// VLC 3.x C ABI, loaded dynamically: no libVLC import library is required.
// All library calls and lifetime operations happen on this one worker.
namespace url_radio {
namespace {
struct libvlc_instance_t;
struct libvlc_media_t;
struct libvlc_media_player_t;
struct Api {
    HMODULE dll = nullptr;
    libvlc_instance_t* (__cdecl *create)(int, const char* const*) = nullptr;
    void (__cdecl *release)(libvlc_instance_t*) = nullptr;
    const char* (__cdecl *version)() = nullptr;
    libvlc_media_t* (__cdecl *media)(libvlc_instance_t*, const char*) = nullptr;
    void (__cdecl *media_release)(libvlc_media_t*) = nullptr;
    libvlc_media_player_t* (__cdecl *player)(libvlc_instance_t*) = nullptr;
    void (__cdecl *player_release)(libvlc_media_player_t*) = nullptr;
    void (__cdecl *set_media)(libvlc_media_player_t*, libvlc_media_t*) = nullptr;
    int (__cdecl *play)(libvlc_media_player_t*) = nullptr;
    void (__cdecl *stop)(libvlc_media_player_t*) = nullptr;
    int (__cdecl *volume)(libvlc_media_player_t*, int) = nullptr;
    int (__cdecl *state)(libvlc_media_player_t*) = nullptr;
};
enum class Kind { Play, Stop, Volume };
struct Command { Kind kind; std::string url, directory; int volume = 50; };
struct State {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Command> commands;
    std::deque<std::string> messages;
    std::thread thread;
    bool exit = false;
};
// Process lifetime storage avoids joining a thread in DLL detach / loader lock.
// CL_Shutdown explicitly joins it while the engine is still running normally.
State& shared() { static State* state = new State; return *state; }
void report(const std::string& msg) {
    auto& s = shared();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.messages.size() >= 16) s.messages.pop_front();
    s.messages.push_back(msg);
}
template<class T> bool resolve(HMODULE dll, T& result, const char* name) {
    FARPROC address = GetProcAddress(dll, name);
    static_assert(sizeof(result) == sizeof(address), "Win32 function pointer size mismatch");
    // GetProcAddress is a generic stdcall pointer; preserve its address while
    // invoking through the VLC export's actual cdecl prototype.
    std::memcpy(&result, &address, sizeof(result));
    return result != nullptr;
}
bool load(Api& a, const std::string& directory) {
    char full[MAX_PATH];
    const std::string path = directory + "\\libvlc.dll";
    DWORD length = GetFullPathNameA(path.c_str(), MAX_PATH, full, nullptr);
    if (!length || length >= MAX_PATH) { report("Invalid VLC runtime directory."); return false; }
    a.dll = LoadLibraryExA(full, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!a.dll) {
        report("Cannot load 32-bit VLC 3.x libvlc.dll (Windows error " + std::to_string(GetLastError()) +
               "). Set _sofbuddy_radio_vlc_dir to the extracted win32 VLC folder.");
        return false;
    }
    bool ok = resolve(a.dll, a.create, "libvlc_new") &&
        resolve(a.dll, a.release, "libvlc_release") &&
        resolve(a.dll, a.version, "libvlc_get_version") &&
        resolve(a.dll, a.media, "libvlc_media_new_location") &&
        resolve(a.dll, a.media_release, "libvlc_media_release") &&
        resolve(a.dll, a.player, "libvlc_media_player_new") &&
        resolve(a.dll, a.player_release, "libvlc_media_player_release") &&
        resolve(a.dll, a.set_media, "libvlc_media_player_set_media") &&
        resolve(a.dll, a.play, "libvlc_media_player_play") &&
        resolve(a.dll, a.stop, "libvlc_media_player_stop") &&
        resolve(a.dll, a.volume, "libvlc_audio_set_volume") &&
        resolve(a.dll, a.state, "libvlc_media_player_get_state");
    if (!ok || !a.version() || a.version()[0] != '3' || a.version()[1] != '.') {
        report("This feature requires the VLC 3.x runtime API.");
        FreeLibrary(a.dll); a.dll = nullptr; return false;
    }
    return true;
}
void worker() {
    Api api;
    libvlc_instance_t* instance = nullptr;
    libvlc_media_player_t* player = nullptr;
    int previous_state = -1;
    bool tracking = false;
    auto cleanup = [&]() {
        if (player) { api.stop(player); api.player_release(player); player = nullptr; }
        if (instance) { api.release(instance); instance = nullptr; }
        if (api.dll) { FreeLibrary(api.dll); api.dll = nullptr; }
    };
    for (;;) {
        Command command;
        bool have_command = false;
        {
            auto& s = shared();
            std::unique_lock<std::mutex> lock(s.mutex);
            s.changed.wait_for(lock, std::chrono::milliseconds(100), [&]() { return s.exit || !s.commands.empty(); });
            if (s.exit) break;
            if (!s.commands.empty()) { command = std::move(s.commands.front()); s.commands.pop_front(); have_command = true; }
        }
        if (have_command) {
            if (command.kind == Kind::Stop) {
                if (player) api.stop(player);
                tracking = false;
                report("Stopped.");
            } else if (command.kind == Kind::Volume) {
                if (player) api.volume(player, command.volume);
            } else {
                // Recreate on each play so a changed runtime directory is respected.
                cleanup();
                tracking = false;
                if (!load(api, command.directory)) continue;
                const char* args[] = {"--no-video", "--no-video-title-show", "--quiet", "--network-caching=1500"};
                instance = api.create(4, args);
                if (!instance) { report("VLC initialization failed; include its plugins folder."); cleanup(); continue; }
                player = api.player(instance);
                if (!player) { report("VLC could not create an audio player."); cleanup(); continue; }
                libvlc_media_t* media = api.media(instance, command.url.c_str());
                if (!media) { report("VLC could not open the stream URL."); cleanup(); continue; }
                api.set_media(player, media);
                api.media_release(media);
                api.volume(player, command.volume);
                if (api.play(player) != 0) { report("VLC rejected stream playback."); cleanup(); continue; }
                previous_state = -1;
                tracking = true;
                report("Connecting to stream...");
            }
        }
        if (player && tracking) {
            int state = api.state(player);
            if (state != previous_state) {
                previous_state = state;
                // libvlc_state_t values are stable in VLC 3.x.
                if (state == 1) report("Opening stream...");
                else if (state == 2) report("Buffering...");
                else if (state == 3) report("Playing.");
                else if (state == 4) report("Paused.");
                else if (state == 6) { report("Stream ended."); tracking = false; }
                else if (state == 7) { report("Stream failed. Check the direct audio URL, network and codecs."); tracking = false; }
            }
        }
    }
    cleanup();
}
void enqueue(Command command, bool start) {
    auto& s = shared();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.thread.joinable()) {
        if (!start) return;
        s.exit = false;
        try { s.thread = std::thread(worker); }
        catch (...) { s.messages.push_back("Cannot create streaming worker."); return; }
    }
    // Play and stop supersede queued work; at most one pending request survives.
    if (command.kind != Kind::Volume) s.commands.clear();
    if (s.commands.size() >= 16) s.commands.pop_front();
    s.commands.push_back(std::move(command));
    s.changed.notify_one();
}
} // namespace
void backend_play(const std::string& url, const std::string& runtime_dir, int volume) {
    enqueue({Kind::Play, url, runtime_dir, volume}, true);
}
void backend_stop() { enqueue({Kind::Stop, "", "", 0}, false); }
void backend_volume(int volume) { enqueue({Kind::Volume, "", "", volume}, false); }
std::vector<std::string> backend_messages() {
    auto& s = shared(); std::lock_guard<std::mutex> lock(s.mutex);
    std::vector<std::string> result(s.messages.begin(), s.messages.end());
    s.messages.clear(); return result;
}
void backend_shutdown() {
    auto& s = shared();
    { std::lock_guard<std::mutex> lock(s.mutex); s.exit = true; s.changed.notify_one(); }
    if (s.thread.joinable()) s.thread.join();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.commands.clear(); s.messages.clear();
}
} // namespace url_radio
#endif
