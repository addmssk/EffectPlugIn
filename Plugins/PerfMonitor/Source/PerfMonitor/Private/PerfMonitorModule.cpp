// PerfMonitor module boilerplate. The module has no startup logic of its own:
// projects create and Initialize() their FPerfMonitorBase-derived monitor
// (typically at GameInstance creation).

#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, PerfMonitor);
