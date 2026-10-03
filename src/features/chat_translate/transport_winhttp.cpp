#include "feature_config.h"
#if FEATURE_CHAT_TRANSLATE
#include "transport.h"
#include <windows.h>
#include <winhttp.h>
#include <chrono>
namespace chat_translate {
using Clock = std::chrono::steady_clock;
struct Internet {
    HINTERNET handle;
    explicit Internet(HINTERNET h) : handle(h) {}
    ~Internet() { if (handle) WinHttpCloseHandle(handle); }
};
TransportReply translate_request(const std::string& key, const std::string& text) {
    TransportReply reply;
    Internet session(WinHttpOpen(L"SoFBuddy-Translate/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.handle) return reply;
    WinHttpSetTimeouts(session.handle, 3000, 3000, 5000, 5000);
    // Use Windows certificate validation. Never relax verification or redirect
    // authenticated requests to another host. Fixed DeepL API Free endpoint.
    DWORD tls = 0x00000800; // WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
    WinHttpSetOption(session.handle, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls, sizeof(tls));
    Internet connection(WinHttpConnect(session.handle, L"api-free.deepl.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.handle) return reply;
    Internet request(WinHttpOpenRequest(connection.handle, L"POST", L"/v2/translate", nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.handle) return reply;
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    std::wstring headers = L"Content-Type: application/json\r\nAuthorization: DeepL-Auth-Key ";
    headers.append(key.begin(), key.end());
    headers += L"\r\n";
    const auto body = request_body(text);
    if (!WinHttpSendRequest(request.handle, headers.c_str(), static_cast<DWORD>(headers.size()),
                            const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                            static_cast<DWORD>(body.size()), 0) || !WinHttpReceiveResponse(request.handle, nullptr)) return reply;
    DWORD size = sizeof(reply.status);
    if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &reply.status, &size, WINHTTP_NO_HEADER_INDEX)) return reply;
    if (reply.status != 200) return reply;
    std::string response;
    char buffer[4096];
    const auto deadline = Clock::now() + std::chrono::seconds(15);
    while (Clock::now() < deadline) {
        DWORD read = 0;
        if (!WinHttpReadData(request.handle, buffer, sizeof(buffer), &read)) return reply;
        if (!read) { reply.valid = parse_response(response, reply.translation); return reply; }
        if (response.size() + read > 65536) return reply;
        response.append(buffer, read);
    }
    return reply;
}
}
#endif
