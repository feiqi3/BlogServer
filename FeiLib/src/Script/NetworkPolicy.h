#pragma once
#include <string_view>
namespace Fei::Script {
// Conservative public-unicast policy. Applied to the actual DNS results by FeiLib.
bool allowPublicAddress(std::string_view host, std::string_view address);
}
