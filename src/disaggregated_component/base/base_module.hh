#ifndef __DISAGREGATED_COMPONENT_BASE_BASE_MODULE_HH__
#define __DISAGREGATED_COMPONENT_BASE_BASE_MODULE_HH__

#include "disaggregated_component/disaggregated_system.hh"
#include "params/BaseModule.hh"
#include "sim/system.hh"

/*
    Role of BaseModule
    1. Forward RequestorId related tasks to DisaggregatedSystem
*/

class BaseModule : public System{

private:
    DisaggregatedSystem* disaggregatedSystem;

public:
    typedef BaseModuleParams Params;
    BaseModule(Params* p);

    DisaggregatedSystem* getSystem(){ return disaggregatedSystem;}

    // For forwarding requestor-id related tasks
    RequestorID getRequestorId(const SimObject* requestor,
        std::string subrequestor = std::string()) override;

    RequestorID getGlobalRequestorId(const std::string& requestor_name) override;

    std::string getRequestorName(RequestorID requestor_id) override;

    RequestorID lookupRequestorId(const SimObject* obj) const override;

    RequestorID lookupRequestorId(const std::string& name) const override;

    RequestorID maxRequestors() override;

};

#endif
