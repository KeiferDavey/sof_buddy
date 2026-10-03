#include "feature_config.h"
#if FEATURE_URL_RADIO
#include "generated_detours.h"
#include "sof_compat.h"
#include "util.h"
#include "backend.h"
#include "protocol.h"
#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace {
cvar_t *enabled, *allow_server, *volume, *directory, *channel;
bool ready = false, server_owned = false, connected_before = false;
bool allowed_before = false, enabled_before = true;
int previous_volume = -1, previous_channel = -1;
std::string last_request;
void print(const std::string& value) {
    if (detour_Com_Printf::oCom_Printf)
        detour_Com_Printf::oCom_Printf("[url_radio] %s\n", value.c_str());
}
void status(const std::string& value) {
    if (detour_Cvar_Set2::oCvar_Set2)
        detour_Cvar_Set2::oCvar_Set2(const_cast<char*>("_sofbuddy_radio_status"), const_cast<char*>(value.c_str()), true);
}
int clamped_volume() {
    if (!volume || !std::isfinite(volume->value)) return 50;
    return static_cast<int>(std::max(0.0f, std::min(100.0f, volume->value)));
}
void play(const std::string& url, bool from_server) {
    if (!enabled || enabled->value == 0) { print("Disabled: set _sofbuddy_radio_enabled 1."); return; }
    if (!url_radio::valid_url(url)) { print("Expected a direct HTTP(S) audio URL, max 384 characters, with spaces percent-encoded."); return; }
    server_owned = from_server;
    url_radio::backend_play(url, directory && directory->string ? directory->string : "sof_buddy/vlc", clamped_volume());
    status("connecting");
}
void stop() { url_radio::backend_stop(); server_owned = false; status("stopped"); }
void cmd_play() {
    if (detour_Cmd_Argc::oCmd_Argc() != 2) { print("Usage: sofbuddy_radio_play \"https://host/stream\""); return; }
    play(detour_Cmd_Argv::oCmd_Argv(1), false);
}
void cmd_stop() { stop(); }
void cmd_volume() {
    if (detour_Cmd_Argc::oCmd_Argc() != 2) { print("Usage: sofbuddy_radio_volume 0..100"); return; }
    const char* value = detour_Cmd_Argv::oCmd_Argv(1);
    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (!value[0] || !end || *end || parsed < 0 || parsed > 100) { print("Volume must be an integer from 0 to 100."); return; }
    detour_Cvar_Set2::oCvar_Set2(const_cast<char*>("_sofbuddy_radio_volume"), const_cast<char*>(value), true);
    url_radio::backend_volume(static_cast<int>(parsed));
}
void cmd_status() {
    cvar_t* state = detour_Cvar_Get::oCvar_Get("_sofbuddy_radio_status", "idle", 0, nullptr);
    print(state && state->string ? state->string : "idle");
}
void clear_channel(int index) {
    if (index < 0 || index > 99) return;
    const std::string name = "_sp_cl_sv_" + std::to_string(index);
    detour_Cvar_Set2::oCvar_Set2(const_cast<char*>(name.c_str()), const_cast<char*>(""), true);
}
} // namespace

void url_radio_PostCvarInit() {
    enabled = detour_Cvar_Get::oCvar_Get("_sofbuddy_radio_enabled", "1", CVAR_SOFBUDDY_ARCHIVE, nullptr);
    allow_server = detour_Cvar_Get::oCvar_Get("_sofbuddy_radio_allow_server", "0", CVAR_SOFBUDDY_ARCHIVE, nullptr);
    volume = detour_Cvar_Get::oCvar_Get("_sofbuddy_radio_volume", "50", CVAR_SOFBUDDY_ARCHIVE, nullptr);
    directory = detour_Cvar_Get::oCvar_Get("_sofbuddy_radio_vlc_dir", "sof_buddy/vlc", CVAR_SOFBUDDY_ARCHIVE, nullptr);
    channel = detour_Cvar_Get::oCvar_Get("_sofbuddy_radio_channel", "90", CVAR_SOFBUDDY_ARCHIVE, nullptr);
    detour_Cvar_Get::oCvar_Get("_sofbuddy_radio_status", "idle", 0, nullptr);
    detour_Cmd_AddCommand::oCmd_AddCommand(const_cast<char*>("sofbuddy_radio_play"), cmd_play);
    detour_Cmd_AddCommand::oCmd_AddCommand(const_cast<char*>("sofbuddy_radio_stop"), cmd_stop);
    detour_Cmd_AddCommand::oCmd_AddCommand(const_cast<char*>("sofbuddy_radio_volume"), cmd_volume);
    detour_Cmd_AddCommand::oCmd_AddCommand(const_cast<char*>("sofbuddy_radio_status"), cmd_status);
    ready = true;
}

void url_radio_frame_post_callback(int msec) {
    (void)msec;
    if (!ready) return;
    const int vol = clamped_volume();
    if (vol != previous_volume) { previous_volume = vol; url_radio::backend_volume(vol); }
    const bool is_enabled = enabled->value != 0;
    if (!is_enabled && enabled_before) stop();
    enabled_before = is_enabled;
    const bool allowed = enabled->value != 0 && allow_server->value != 0;
    if (!allowed && server_owned) stop();
    if (allowed != allowed_before) { last_request.clear(); allowed_before = allowed; }

    // Same engine connection-state RVA used by the existing http_maps feature.
    static int* cls_state = static_cast<int*>(rvaToAbsExe(reinterpret_cast<void*>(0x001C1F00)));
    const bool connected = cls_state && (*cls_state == ca_connected || *cls_state == ca_active);
    int index = -1;
    if (std::isfinite(channel->value) && channel->value >= 0 && channel->value <= 99 &&
        std::floor(channel->value) == channel->value) index = static_cast<int>(channel->value);
    if (previous_channel != index) { last_request.clear(); previous_channel = index; }
    if (!connected) {
        if (server_owned) stop();
        if (connected_before || !last_request.empty()) { clear_channel(index); last_request.clear(); }
    } else if (index >= 0) {
        const std::string name = "_sp_cl_sv_" + std::to_string(index);
        cvar_t* incoming = detour_Cvar_Get::oCvar_Get(name.c_str(), "", 0, nullptr);
        const std::string value = incoming && incoming->string ? incoming->string : "";
        if (value != last_request) {
            last_request = value;
            url_radio::Request request;
            if (allowed && url_radio::parse_request(value, request)) {
                if (request.play) play(request.url, true);
                else if (server_owned) stop();
            }
        }
    }
    connected_before = connected;
    for (const auto& message : url_radio::backend_messages()) { print(message); status(message); }
}

void url_radio_shutdown_pre_callback() {
    ready = false;
    url_radio::backend_shutdown();
}
#endif
