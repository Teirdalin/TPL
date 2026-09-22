#pragma once
#include <string>
namespace tpl {
bool installUi(void (*ready)());
void runUpdater(bool force,bool checkOnly=false);
bool updaterRunning();
}
