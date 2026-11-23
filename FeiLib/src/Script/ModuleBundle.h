#pragma once
#include "Script/FScriptEngine.h"
namespace Fei::Script {
using ModuleBundle=Fei::FScriptSource;
ModuleBundle readModuleBundle(const std::string& file);
}
