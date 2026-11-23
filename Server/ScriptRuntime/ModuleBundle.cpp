#include "ModuleBundle.h"
namespace Blog::Script {
ModuleBundle readModuleBundle(const std::string& file){return Fei::FScriptEngine::readSource(file);}
}
