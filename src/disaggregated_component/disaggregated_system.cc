#include "disaggregated_component/disaggregated_system.hh"

DisaggregatedSystem::DisaggregatedSystem(Params* p):
    SimObject(p)
{
    // (For Testing) Hard coded initialization of
    // transltion info and location map

    /* Currently hard-coded CIDs:
     * CPUs - CID 0 ~ (# of CPUs - 1)
     * Memory pool manager - Last CPU's CID + 1
     *      Memory pool controller - subCID 0 ~
     */ 

    // Assign location of modules
    // locationMap: (switch id, vector of attached CIDs)
    std::vector<uint32_t> s0Neighbors;
    for (uint32_t i = 0 ; i <= p->num_cpu_modules ; i++) {
        s0Neighbors.push_back(i);
    }
    locationMap.emplace(0, s0Neighbors);

    // Map each CPU to its memory controllers
    // MemPoolCtrl's subCID starts from 0 and increases globally
    uint32_t mem_pool_mgr_cid = p->num_cpu_modules;
    uint32_t mem_ctrl_subcid = 0;
    uint32_t size_per_mem_ctrl = p->size_per_mem_ctrl;
    for (uint32_t cpu_cid = 0 ; cpu_cid < p->num_cpu_modules ; cpu_cid++) {
        Addr interleave_unit = p->interleave_unit; // default: 4KB page
        TranslationInformation info(interleave_unit);

        // Memory controller interleaving mode
        if (p->mem_interleave) {
            Addr addr = 0;
            for (uint32_t mem_ctrl_per_cpu = 0 ; mem_ctrl_per_cpu < p->num_mem_ctrls; mem_ctrl_per_cpu++) {
                printf("[INFO] Initializing CID mapping (interleaving): %d CID to %d subCID\n", cpu_cid, mem_ctrl_subcid);
                info.recvMap(addr, addr, size_per_mem_ctrl); // Currently disabled in MemModuleConfig.py
                info.sendMap(addr, addr, size_per_mem_ctrl, mem_pool_mgr_cid, mem_ctrl_subcid, p->num_mem_ctrls);

                // Next memory pool controller
                addr += interleave_unit; // Interleaving: see translation_information.cc
                mem_ctrl_subcid++;
            }
        }


        // Non-interleaving mode
        else {
            Addr addr = 0;
            for (uint32_t mem_ctrl_per_cpu = 0 ; mem_ctrl_per_cpu < p->num_mem_ctrls; mem_ctrl_per_cpu++) {
                printf("[INFO] Initializing CID mapping (non-interleaving): %d CID to %d subCID\n", cpu_cid, mem_ctrl_subcid);
                info.recvMap(addr, addr, size_per_mem_ctrl); // Currently disabled in MemModuleConfig.py
                info.sendMap(addr, addr, size_per_mem_ctrl, mem_pool_mgr_cid, mem_ctrl_subcid);

                // Next memory pool controller
                addr += size_per_mem_ctrl;
                mem_ctrl_subcid++;
            }
        }

        translationInformation.emplace(cpu_cid, info);
    }

    // Translation information for memory pool manager
    TranslationInformation mem_pool_mgr_info(p->interleave_unit);
    translationInformation.emplace(mem_pool_mgr_cid, mem_pool_mgr_info);
}

std::string
DisaggregatedSystem::stripSystemName(const std::string& requestor_name) const
{
    if (startswith(requestor_name, name())) {
        return requestor_name.substr(name().size());
    } else {
        return requestor_name;
    }
}

RequestorID
DisaggregatedSystem::lookupRequestorId(const SimObject* obj) const
{
    RequestorID id = Request::invldRequestorId;

    // number of occurrences of the SimObject pointer
    // in the requestor list.
    auto obj_number = 0;

    for (int i = 0; i < requestors.size(); i++) {
        if (requestors[i].obj == obj) {
            id = i;
            obj_number++;
        }
    }

    fatal_if(obj_number > 1,
        "Cannot lookup RequestorID by SimObject pointer: "
        "More than one requestor is sharing the same SimObject\n");

    return id;
}

RequestorID
DisaggregatedSystem::lookupRequestorId(const std::string& requestor_name) const
{
    std::string name = stripSystemName(requestor_name);

    for (int i = 0; i < requestors.size(); i++) {
        if (requestors[i].req_name == name) {
            return i;
        }
    }

    return Request::invldRequestorId;
}

RequestorID
DisaggregatedSystem::getGlobalRequestorId(const std::string& requestor_name)
{
    return _getRequestorId(nullptr, requestor_name);
}

RequestorID
DisaggregatedSystem::getRequestorId
    (const SimObject* requestor, std::string subrequestor)
{
    auto requestor_name = leafRequestorName(requestor, subrequestor);
    return _getRequestorId(requestor, requestor_name);
}

RequestorID
DisaggregatedSystem::_getRequestorId
    (const SimObject* requestor,const std::string& requestor_name)
{
    std::string name = stripSystemName(requestor_name);

    // CPUs in switch_cpus ask for ids again after switching
    for (int i = 0; i < requestors.size(); i++) {
        if (requestors[i].req_name == name) {
            return i;
        }
    }

    // Verify that the statistics haven't been enabled yet
    // Otherwise objects will have sized their stat buckets and
    // they will be too small

    if (Stats::enabled()) {
        fatal("Can't request a requestorId after regStats(). "
                "You must do so in init().\n");
    }

    // Generate a new RequestorID incrementally
    RequestorID requestor_id = requestors.size();

    // Append the new Requestor metadata to the group of system Requestors.
    requestors.emplace_back(requestor, name, requestor_id);

    return requestors.back().id;
}

std::string
DisaggregatedSystem::leafRequestorName
    (const SimObject* requestor, const std::string& subrequestor)
{
    if (subrequestor.empty()) {
        return requestor->name();
    } else {
        // Get the full requestor name by appending the subrequestor name to
        // the root SimObject requestor name
        return requestor->name() + "." + subrequestor;
    }
}

std::string
DisaggregatedSystem::getRequestorName(RequestorID requestor_id)
{
    if (requestor_id >= requestors.size())
        fatal("Invalid requestor_id passed to getRequestorName()\n");

    const auto& requestor_info = requestors[requestor_id];
    return requestor_info.req_name;
}

DisaggregatedSystem*
DisaggregatedSystemParams::create()
{
    return new DisaggregatedSystem(this);
}
