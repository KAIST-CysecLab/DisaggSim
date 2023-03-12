#ifndef __DISAGREGATED_COMPONENT_MEMORY_MODULE_MEMORY_SUB_MODULE_HH__
#define __DISAGREGATED_COMPONENT_MEMORY_MODULE_MEMORY_SUB_MODULE_HH__

#include "disaggregated_component/base/base_module.hh"
#include "params/MemorySubModule.hh"

class MemorySubModule : public BaseModule{

public:

    void init() override;

    typedef MemorySubModuleParams Params;
    MemorySubModule(Params* p);

};

#endif
