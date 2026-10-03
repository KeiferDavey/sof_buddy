#include "features/url_radio/protocol.h"
#include <cassert>
#include <iostream>
using namespace url_radio;
int main() {
    Request r;
    assert(parse_request("SB_RADIO_V1|1|play|https://example.com/live.mp3", r));
    assert(r.play && r.url == "https://example.com/live.mp3" && r.nonce == "1");
    assert(parse_request("SB_RADIO_V1|2|stop", r) && !r.play && r.url.empty());
    assert(valid_url("http://example.com:8000/radio?token=abc%20def&v=1"));
    const char* invalid[] = {"file:///C:/test", "https://", "https:///test", "https://user:pass@host/a", "https://host/a\nquit", "https://host/a;quit", "https://host/a|stop", "https://host/a b", "https://host/\"", "https://host/\\a"};
    for (auto value : invalid) assert(!valid_url(value));
    const char* malformed[] = {"", "play|https://example.com", "SB_RADIO_V1||stop", "SB_RADIO_V1|x|stop", "SB_RADIO_V1|1|volume|100", "SB_RADIO_V1|1|stop|extra", "SB_RADIO_V1|1|play|file:///test"};
    for (auto value : malformed) assert(!parse_request(value, r));
    assert(!valid_url("https://host/" + std::string(kMaxUrl, 'a')));
    assert(!parse_request("SB_RADIO_V1|" + std::string(21, '1') + "|stop", r));
    assert(!parse_request(std::string(kMaxRequest + 1, 'a'), r));
    std::cout << "URL and server protocol tests passed\n";
}
