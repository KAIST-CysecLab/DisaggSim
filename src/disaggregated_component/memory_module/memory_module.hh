#ifndef __DISAGREGATED_COMPONENT_MEMORY_MODULE_MEMORY_MODULE_HH__
#define __DISAGREGATED_COMPONENT_MEMORY_MODULE_MEMORY_MODULE_HH__

#include "disaggregated_component/base/base_module.hh"
#include "params/MemoryModule.hh"

class MemoryModule : public BaseModule{

public:

    void init() override;

    typedef MemoryModuleParams Params;
    MemoryModule(Params* p);

};

#endif
