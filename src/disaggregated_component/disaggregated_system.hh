#ifndef __DISAGREGATED_COMPONENT_DISAGGREGATED_SYSTEM_HH__
#define __DISAGREGATED_COMPONENT_DISAGGREGATED_SYSTEM_HH__

#include <map>

#include "disaggregated_component/translation_information.hh"
#include "params/DisaggregatedSystem.hh"
#include "sim/sim_object.hh"
#include "sim/system.hh"

/*
    Role of DisaggregatedSystem class:
    1. Management of requestor id (referred System class)
    2. Management of resource mappings
*/
class DisaggregatedSystem : public SimObject{

private:

    // location of modules
    // key: switch id
    // value: list of attached component ids
    std::map<uint32_t, std::vector<uint32_t>> locationMap;

    // allocation maps
    // key: component id
    // value: translation information (i.e. two translation tables)
    std::map<uint32_t, TranslationInformation> translationInformation;

    std::vector<RequestorInfo> requestors;

    std::string stripSystemName(const std::string& requestor_name) const;

    RequestorID _getRequestorId
        (const SimObject* requestor, const std::string& requestor_name);

    std::string leafRequestorName
        (const SimObject* requestor, const std::string& subrequestor);

public:
    typedef DisaggregatedSystemParams Params;
    DisaggregatedSystem(Params* p);

    RequestorID getRequestorId
        (const SimObject* requestor, std::string subrequestor = std::string());

    RequestorID getGlobalRequestorId(const std::string& requestor_name);

    std::string getRequestorName(RequestorID requestor_id);

    RequestorID lookupRequestorId(const SimObject* obj) const;

    RequestorID lookupRequestorId(const std::string& name) const;

    RequestorID maxRequestors() { return requestors.size(); }

    TranslationInformation* getTranslationInformation(uint32_t cid)
    {return &translationInformation.at(cid);}

};

#endif
