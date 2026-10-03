#pragma once
#include <string>
#include <vector>
namespace url_radio {
void backend_play(const std::string& url, const std::string& runtime_dir, int volume);
void backend_stop();
void backend_volume(int volume);
std::vector<std::string> backend_messages();
void backend_shutdown();
}
