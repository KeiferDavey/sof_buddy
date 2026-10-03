#pragma once
#include "protocol.h"
namespace chat_translate {
struct TransportReply { unsigned long status = 0; Translation translation; bool valid = false; };
TransportReply translate_request(const std::string& key, const std::string& text);
}
