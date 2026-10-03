#include "features/chat_translate/protocol.h"
#include "features/chat_translate/capture.h"
#include <cassert>
#include <iostream>
#include <cstdarg>
#include <string>
using namespace chat_translate;
std::string formatted(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    auto result = format_print(fmt, args);
    va_end(args); return result;
}
int main() {
    Chat chat;
    assert(parse_chat("Julien: Bonjour, bonne partie!\n", chat));
    assert(chat.speaker == "Julien" && chat.text == "Bonjour, bonne partie!");
    assert(parse_chat("(Julien): Bonjour!\r\n", chat));
    assert(parse_chat("\1Julien: Salut!\n", chat));
    assert(parse_chat("[TAG]GaLLo: ciao!", chat));
    for (const auto& text : {"connected", "Server: Loading map", "http: example", "ERROR: crash",
                            "[translate] Julien [FR -> EN]: Hello", "Julien: \n", "Julien: 12345",
                            "Julien: salut\nServer: bad"}) assert(!parse_chat(text, chat));
    assert(!parse_chat(std::string(65, 'a') + ": bonjour", chat));
    assert(!parse_chat("Player: " + std::string(513, 'a'), chat));
    assert(parse_chat("\3[KK]K\x88\xcc\x8a\x88R\2 : \4[0] bonjour, bonne\n", chat));
    assert(chat.speaker == "[KK]K\x88\xcc\x8a\x88R" && chat.text == "bonjour, bonne");
    assert(parse_chat("Player : [31] hola!", chat) && chat.text == "hola!");
    assert(parse_chat("Player : [32] hola!", chat) && chat.text == "[32] hola!");
    assert(parse_chat("Player: bon\3jour", chat) && chat.text == "bonjour");
    LineBuffer lines;
    assert(lines.append("Player").empty());
    assert(lines.append(" : [0] bon").empty());
    const auto split = lines.append("jour\nPlayer: hola\r\n");
    assert(split.size() == 2 && split[0] == "Player : [0] bonjour" && split[1] == "Player: hola");
    assert(parse_chat(split[0], chat) && chat.text == "bonjour");
    assert(lines.append("Player: ciao").empty());
    assert(lines.flush()[0] == "Player: ciao");
    lines.append(std::string(1025, 'x'));
    assert(lines.append("overflow\n").empty());
    assert(lines.append("Player: good\n").size() == 1);
    unsigned char call[] = {0xe8, 0xfb, 0x0f, 0x00, 0x00, 0x83, 0xc4, 0x04};
    assert(console_call_verified(call, sizeof(call), 0x1000, 0x2000));
    assert(!console_call_verified(call, sizeof(call), 0x1000, 0x3000));
    call[7] = 8;
    assert(!console_call_verified(call, sizeof(call), 0x1000, 0x2000));
    const unsigned char actual_call[] = {0xe8, 0x46, 0x43, 0x00, 0x00,
        0x8b, 0x15, 0x4c, 0x9f, 0x24, 0x20, 0x83, 0xc4, 0x04};
    assert(console_call_verified(actual_call, sizeof(actual_call), 0x2001c795, 0x20020ae0));
    assert(!console_call_verified(actual_call, sizeof(actual_call)-1, 0x2001c795, 0x20020ae0));
    assert(!console_call_verified(actual_call, sizeof(actual_call), 0x2001c795, 0x20020ae1));
    for (const auto& line : {"Client [1]: sp_sv_client_swap", "Incoming [1]: starjump", "Player : [0] Hi!", "Player: lol", "Player: okay"}) assert(!parse_chat(line, chat));
    assert(parse_chat("Player : [0] thats funny as fuck", chat));
    assert(parse_chat("Player : [0] va te faire foutre", chat));
    assert(parse_chat("Player : [0] bonjour", chat));
    assert(parse_chat("Other : [3] bonjour", chat) && chat.slot == 3);
    assert(eligible_slot(chat, 0, false));
    assert(!eligible_slot(chat, 3, false));
    assert(eligible_slot(chat, 3, true));
    assert(!eligible_slot(chat, -1, false));
    assert(eligible_slot(chat, -1, true));
    assert(parse_chat("Other : [31] bonjour", chat) && eligible_slot(chat, 0, false));
    assert(parse_chat("Other : [32] bonjour", chat) && chat.slot == -1 && !eligible_slot(chat, 0, true));
    assert(parse_chat("Other: bonjour", chat) && chat.slot == -1 && !eligible_slot(chat, 0, true));
    assert(parse_chat("Other : [-1] bonjour", chat) && !eligible_slot(chat, 0, true));
    const auto body = request_body("Bonjour \"ami\"\\\n\t\xc3\xa9");
    picojson::value parsed;
    assert(picojson::parse(parsed, body).empty());
    assert(parsed.get("target_lang").get<std::string>() == "EN-GB");
    assert(parsed.get("text").get<picojson::array>()[0].get<std::string>() == "Bonjour \"ami\"\\\n\t\xc3\xa9");
    assert(!parsed.contains("source_lang"));
    Translation result;
    assert(parse_response(R"({"translations":[{"detected_source_language":"FR","text":"Hello, \"friend\"! \u00e9 \ud83d\ude00"}]})", result));
    assert(result.language == "FR" && result.text == "Hello, \"friend\"! \xc3\xa9 \xf0\x9f\x98\x80");
    assert(parse_response(R"({"translations":[{"detected_source_language":"EN","text":"Hello"}]})", result));
    for (const auto& json : {"{}", "{", "[]", "{\"translations\":[]}",
         R"({"translations":[{"text":"bad"}]})", R"({"translations":[{"text":42,"detected_source_language":"FR"}]})",
         R"({"translations":[{"text":"hi","detected_source_language":"FR\nBAD"}]})"}) assert(!parse_response(json, result));
    assert(!parse_response(std::string(65537, 'x'), result));
    assert(safe_display("hi\n\1there\t%") == "hi  there %");
    assert(safe_display(std::string(900, 'x')).size() == 768);
    assert(valid_key("00000000-0000-0000-0000-000000000000:fx"));
    assert(!valid_key("bad"));
    assert(!valid_key("00000000-0000\r\nAuthorization: injected"));
    assert(formatted("%s: %d %.2f %% %08x", "name", 42, 1.25, 255) == "name: 42 1.25 % 000000ff");
    const std::string long_message(12000, 'x');
    assert(formatted("%s", long_message.c_str()) == long_message);
    assert(formatted("%s", "User: 100% %n %s") == "User: 100% %n %s");
    std::cout << "Chat filtering, JSON/Unicode, credential validation and variadic forwarding tests passed.\n";
}
