#include "disaggregated_component/cpu_module/cpu_module.hh"

CPUModule::CPUModule(Params *p):
    BaseModule(p)
{}

CPUModule*
CPUModuleParams::create()
{
    return new CPUModule(this);
}
