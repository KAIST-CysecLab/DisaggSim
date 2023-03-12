#include "mem/ruby/system/ModuleSequencer.hh"

ModuleSequencer::ModuleSequencer(const Params* p)
    : ClockedObject(p), rs(p->ruby_system),
    requestor(p->name + ".requestor", this),
    responder(p->name + ".responder", this)
{
}

void
ModuleSequencer::init()
{
    mandatory_q_ptr = controller->getMandatoryQueue();
    responseFromMemory_ptr = controller->getMemRespQueue();
    responder.sendRangeChange();
}


Port&
ModuleSequencer::getPort(const std::string &if_name, PortID idx)
{
    // This is the name from the Python SimObject declaration in SimpleCache.py
    if (if_name == "responder") {
        return responder;
    } else if (if_name == "requestor" ) {
        return requestor;
    }
    else {
        // pass it along to our super class

        return ClockedObject::getPort(if_name, idx);
    }
}

MachineID
ModuleSequencer::getDstMachineID(PacketPtr pkt, MachineType mtype){
      NodeID node = pkt->req->getDstCID();
      MachineID mach = {mtype, node};
      return mach;
}

bool
ModuleSequencer::functionalRequest(PacketPtr pkt){
    if(pkt->req->getDstCID() != controller->getMachineID().getNum())
    {
        fatal("[DisaggSim-DEBUG] ModuleSequencer::functionalRequest:: component ID not match");
        return false;
    }
    requestor.sendFunctional(pkt);
    return true;
}

void
ModuleSequencer::dataCallback(const PacketPtr& pkt)
{
    // TODO: send with responder
    responder.sendTimingResp(pkt);
}

void
ModuleSequencer::writeCompleteCallback(const PacketPtr& pkt)
{
    responder.sendTimingResp(pkt);
}

void 
ModuleSequencer::requestCallback(const PacketPtr& pkt)
{
    requestor.sendTimingReq(pkt);
}


ModuleSequencer::SequencerRequestPort::SequencerRequestPort
    (const std::string& name, ModuleSequencer *owner) :
    RequestPort(name, owner),
    owner(owner)
{ }

// sending response to another module
// refer to AbstractController::recvTimingResp(PacketPtr pkt)
// set machine id appropriately
bool
ModuleSequencer::SequencerRequestPort::recvTimingResp(PacketPtr pkt){
    //TODO enqueue to responseFromMemory_ptr
    assert(owner->controller->getMemRespQueue());
    assert(pkt->isResponse());

    std::shared_ptr<MemoryMsg> msg = std::make_shared<MemoryMsg>(owner->clockEdge());
    (*msg).m_addr = pkt->getAddr();
    (*msg).m_Sender = owner->controller->getMachineID();

    SenderState *s = dynamic_cast<SenderState *>(pkt->senderState);
    (*msg).m_OriginalRequestorMachId = s->id;
    pkt->popSenderState();
    delete s;

    if (pkt->isRead()) {
        (*msg).m_Type = MemoryRequestType_MEMORY_READ;
        (*msg).m_MessageSize = MessageSizeType_Response_Data;

        // Copy data from the packet
        (*msg).m_DataBlk.setData(pkt->getPtr<uint8_t>(), 0,
                                 RubySystem::getBlockSizeBytes());
        (*msg).m_pkt = pkt;
    } else if (pkt->isWrite()) {
        (*msg).m_Type = MemoryRequestType_MEMORY_WB;
        (*msg).m_MessageSize = MessageSizeType_Writeback_Control;
        (*msg).m_pkt = pkt;
    } else {
        panic("Incorrect packet type received from memory controller!");
    }

    owner->controller->getMemRespQueue()->enqueue(msg, owner->clockEdge(), owner->cyclesToTicks(Cycles(1)));
    return true;
}

ModuleSequencer::SequencerResponsePort::SequencerResponsePort
    (const std::string& name, ModuleSequencer *owner) :
    ResponsePort(name, owner),
    owner(owner)
{ }

// sending request to another module
bool
ModuleSequencer::SequencerResponsePort::recvTimingReq(PacketPtr pkt)
{ 
    uint8_t* data =  pkt->getPtr<uint8_t>();
    bool write = pkt->isWrite();

    std::shared_ptr<RubyRequest> msg = std::make_shared<RubyRequest>(
                                        owner->clockEdge(), pkt->getAddr(),
                                        write? data:0,
                                        pkt->getSize(),
                                        0,
                                        write?RubyRequestType_ST:RubyRequestType_LD,
                                        RubyAccessMode_User,
                                        pkt);

    SenderState *s = new SenderState(owner->controller->getMachineID());
    pkt->pushSenderState(s);

    owner->mandatory_q_ptr->enqueue(msg, owner->clockEdge(), owner->cyclesToTicks(Cycles(1)));
    return true;
}

void
ModuleSequencer::SequencerResponsePort::recvFunctional(PacketPtr pkt)
{
    RubySystem *rs = owner->rs;

    bool accessSucceeded = false;
    bool needsResponse = pkt->needsResponse();

    // Do the functional access on ruby memory
    if (pkt->isRead()) {
        accessSucceeded = rs->functionalRead(pkt);
    } else if (pkt->isWrite()) {
        accessSucceeded = rs->functionalWrite(pkt);
    } else {
        panic("Unsupported functional command %s\n", pkt->cmdString());
    }

    // Unless the requester explicitly said otherwise, generate an error if
    // the functional request failed
    if (!accessSucceeded && !pkt->suppressFuncError()) {
        fatal("Ruby functional %s failed for address %#x\n",
                pkt->isWrite() ? "write" : "read", pkt->getAddr());
    }

    // turn packet around to go back to requester if response expected
    if (needsResponse) {
        // The pkt is already turned into a reponse if the directory
        // forwarded the request to the memory controller (see
        // AbstractController::functionalMemoryWrite and
        // AbstractMemory::functionalAccess)
        if (!pkt->isResponse())
            pkt->makeResponse();
        pkt->setFunctionalResponseStatus(accessSucceeded);
    }
}

ModuleSequencer*
ModuleSequencerParams::create()
{
    return new ModuleSequencer(this);
}
