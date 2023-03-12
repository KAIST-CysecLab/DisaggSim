#include "disaggregated_component/memory_module/memory_sub_module.hh"

MemorySubModule::MemorySubModule(Params *p):
    BaseModule(p)
{}

MemorySubModule*
MemorySubModuleParams::create()
{
    return new MemorySubModule(this);
}

void
MemorySubModule::init()
{
    // removed system port check
    return;
}
