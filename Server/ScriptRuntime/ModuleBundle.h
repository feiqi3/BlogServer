#pragma once
#include "Script/FScriptEngine.h"
namespace Blog::Script {
using ModuleBundle=Fei::FScriptSource;
ModuleBundle readModuleBundle(const std::string& file);
}
