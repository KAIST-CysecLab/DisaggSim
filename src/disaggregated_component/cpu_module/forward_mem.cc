#include "disaggregated_component/cpu_module/forward_mem.hh"

ForwardMemory::ForwardMemory(Params *p):
    AbstractMemory(p),
    externalResponder(p->name + ".external_responder", this, true),
    externalRequestor(p->name + ".external_requestor", this, true),
    internalResponder(p->name + ".internal_responder", this, false),
    internalRequestor(p->name + ".internal_requestor", this, false)
    {}




Port&
ForwardMemory::getPort(const std::string &if_name, PortID idx)
{
    // This is the name from the Python SimObject declaration in SimpleCache.py
    if (if_name == "external_responder") {
        return externalResponder;
    } else if (if_name == "external_requestor") {
        return externalRequestor;
    } else if (if_name == "internal_responder") {
        return internalResponder;
    } else if (if_name == "internal_requestor") {
        return internalRequestor;
    } else {
        return ClockedObject::getPort(if_name, idx);
    }
}

AddrRangeList
ForwardMemory::ForwardResponsePort::getAddrRanges() const
{
    AddrRangeList ranges;
    ranges.push_back(owner->range);
    return ranges;
}

ForwardMemory::ForwardResponsePort::ForwardResponsePort
    (const std::string& name, ForwardMemory *owner, bool isExternal) :
    ResponsePort(name, owner),
    owner(owner),
    isExternal(isExternal)
{ }

void
ForwardMemory::ForwardResponsePort::recvFunctional(PacketPtr pkt)
{
    if (isExternal)
    {
        owner->internalRequestor.sendFunctional(pkt);
    } else
    {
        owner->externalRequestor.sendFunctional(pkt);
    }
}

bool
ForwardMemory::ForwardResponsePort::recvTimingReq(PacketPtr pkt)
{
    if (isExternal)
    {
        return owner->internalRequestor.sendTimingReq(pkt);
    } else
    {
        return owner->externalRequestor.sendTimingReq(pkt);
    }
}

void
ForwardMemory::ForwardResponsePort::recvRespRetry()
{
    if (isExternal)
    {
        return owner->internalRequestor.sendRetryResp();
    } else
    {
        return owner->externalRequestor.sendRetryResp();
    }
}



ForwardMemory::ForwardRequestPort::ForwardRequestPort
    (const std::string& name, ForwardMemory *owner, bool isExternal) :
    RequestPort(name, owner), owner(owner), isExternal(isExternal)
    {}


bool
ForwardMemory::ForwardRequestPort::recvTimingResp(PacketPtr pkt)
{
    if (isExternal)
    {
        return owner->internalResponder.sendTimingResp(pkt);
    } else
    {
        return owner->externalResponder.sendTimingResp(pkt);
    }
}

void
ForwardMemory::ForwardRequestPort::recvReqRetry()
{
    if (isExternal)
    {
        return owner->internalResponder.sendRetryReq();
    } else
    {
        return owner->externalResponder.sendRetryReq();
    }
}

ForwardMemory*
ForwardMemoryParams::create()
{
    return new ForwardMemory(this);
}

void
ForwardMemory::init()
{
    if (internalResponder.isConnected())
        internalResponder.sendRangeChange();
    if (externalResponder.isConnected())
        externalResponder.sendRangeChange();
}
