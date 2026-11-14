#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif
#include "NetworkPolicy.h"
#include <string>
namespace Fei::Script {
bool allowPublicAddress(std::string_view, std::string_view address) {
    std::string text(address);
    unsigned char b[16]{};
    if (inet_pton(AF_INET, text.c_str(), b) == 1) {
        if (b[0] == 0 || b[0] == 10 || b[0] == 127 || b[0] >= 224) return false;
        if (b[0] == 100 && (b[1] & 192) == 64) return false;
        if (b[0] == 169 && b[1] == 254) return false;
        if (b[0] == 172 && b[1] >= 16 && b[1] <= 31) return false;
        if (b[0] == 192 && ((b[1] == 168) || (b[1] == 0 && (b[2] == 0 || b[2] == 2)) || (b[1] == 88 && b[2] == 99))) return false;
        if (b[0] == 198 && (b[1] == 18 || b[1] == 19 || (b[1] == 51 && b[2] == 100))) return false;
        if (b[0] == 203 && b[1] == 0 && b[2] == 113) return false;
        return true;
    }
    if (inet_pton(AF_INET6, text.c_str(), b) != 1 || (b[0] & 224) != 32) return false;
    if (b[0] == 32 && b[1] == 1 && (b[2] < 2 || (b[2] == 13 && b[3] == 184))) return false;
    if (b[0] == 32 && b[1] == 2) return false; // 6to4 can embed private IPv4 destinations.
    if (b[0] == 63 && (b[1] & 240) == 240) return false;
    return true;
}
}
