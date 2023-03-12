#include "disaggregated_component/base/base_module.hh"

BaseModule::BaseModule(Params *p):
    System(p),
    disaggregatedSystem(p->disaggregated_system)
{}


RequestorID
BaseModule::getRequestorId(const SimObject* requestor, std::string subrequestor)
{
    return disaggregatedSystem->getRequestorId(requestor, subrequestor);
}

RequestorID
BaseModule::getGlobalRequestorId(const std::string& requestor_name)
{
    return disaggregatedSystem->getGlobalRequestorId(requestor_name);
}

std::string
BaseModule::getRequestorName(RequestorID requestor_id)
{
    return disaggregatedSystem->getRequestorName(requestor_id);
}

RequestorID
BaseModule::lookupRequestorId(const SimObject* obj) const
{
    return disaggregatedSystem->lookupRequestorId(obj);
}

RequestorID
BaseModule::lookupRequestorId(const std::string& name) const
{
    return disaggregatedSystem->lookupRequestorId(name);
}


RequestorID
BaseModule::maxRequestors()
{
    return disaggregatedSystem->maxRequestors();
}

BaseModule*
BaseModuleParams::create()
{
    return new BaseModule(this);
}
