#include "disaggregated_component/memory_module/memory_module.hh"

MemoryModule::MemoryModule(Params *p):
    BaseModule(p)
{}

MemoryModule*
MemoryModuleParams::create()
{
    return new MemoryModule(this);
}

void
MemoryModule::init()
{
    // removed system port check
    return;
}
