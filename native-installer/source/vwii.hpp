#pragma once
#include "vwii_plan.hpp"
#include <memory>
namespace Vwii {
class Console;
struct Session {std::shared_ptr<Console> console;Plan plan;};
Session PrepareConsole(const std::string &bundle,uint32_t selected,const std::vector<Setup::FileDef> &files,const Setup::Progress &progress);
void InstallConsole(Session &session,const std::string &report,const Setup::Progress &progress);
}
