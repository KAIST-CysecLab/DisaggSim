#ifndef __DISAGREGATED_COMPONENT_CPU_MODULE_CPU_MODULE_HH__
#define __DISAGREGATED_COMPONENT_CPU_MODULE_CPU_MODULE_HH__

#include "disaggregated_component/base/base_module.hh"
#include "params/CPUModule.hh"
#include "sim/system.hh"

class CPUModule : public BaseModule{

public:
    typedef CPUModuleParams Params;
    CPUModule(Params* p);

};

#endif
