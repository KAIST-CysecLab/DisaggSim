#include "disaggregated_component/interconnect_logic.hh"

/*
    Translation Behaviors
    - In real life
        - send req
            - translation required if different address space (between node address space and fabric address space)
        - send resp
            - translation required if different address space
        - receive req
            - translation required if different address space
        - receive resp
            - translation required if different address space
    - In gem5 => translation required only when it handles requests
        - send req
            - translation required if different address space + save the original paddr
        - send resp
            - no translation required => just send
        - receive req
            - translation required if different address space
        - receive resp
            - no translation required => just restore the original paddr 
*/


InterconnectLogic::InterconnectLogic(Params *p):
    ClockedObject(p),
    numSndProcessingUnit(p->num_processing_units),
    numRcvProcessingUnit(p->num_processing_units),
    externalResponder(p->name + ".external_responder", this, true),
    externalRequestor(p->name + ".external_requestor", this, true),
    internalResponder(p->name + ".internal_responder", this, false),
    internalRequestor(p->name + ".internal_requestor", this, false),
    rcvLimit(p->rcv_limit),
    sndLimit(p->snd_limit),
    latency(p->latency),
    module(p->module),
    range(p->range),
    cid(p->cid),
    isRequireReceiveTranslation(p->is_require_rcv_trans),
    isRequireSendTranslation(p->is_require_snd_trans)
{
    inform("CID: %u", cid);
    sndProcessingUnitAvailableTimes = new Tick[p->num_processing_units];
    for(unsigned i =0; i<p->num_processing_units;++i ){
        sndProcessingUnitAvailableTimes[i] = 0; 
    }

    rcvProcessingUnitAvailableTimes = new Tick[p->num_processing_units];
    for(unsigned i =0; i<p->num_processing_units;++i ){
        rcvProcessingUnitAvailableTimes[i] = 0; 
    }
}

InterconnectLogic::~InterconnectLogic(){
    delete[] sndProcessingUnitAvailableTimes;
    delete[] rcvProcessingUnitAvailableTimes;
}

InterconnectLogic::DisaggregatedResponsePort::DisaggregatedResponsePort
    (const std::string& name, InterconnectLogic *owner, bool isExternal) :
    ResponsePort(name, owner),
    needRetry(false),
    owner(owner),
    isExternal(isExternal)
{ }

InterconnectLogic::DisaggregatedRequestPort::DisaggregatedRequestPort
    (const std::string& name, InterconnectLogic *owner, bool isExternal) :
    RequestPort(name, owner),
    needRetry(false),
    owner(owner),
    isExternal(isExternal)
{ }


Port&
InterconnectLogic::getPort(const std::string &if_name, PortID idx)
{
    // This is the name from the Python SimObject declaration in SimpleCache.py
    if (if_name == "external_responder") {
        panic_if(idx != InvalidPortID, "external_resp is not a vector port");
        return externalResponder;
    } else if (if_name == "external_requestor" ) {
        panic_if(idx != InvalidPortID, "external_req is not a vector port");
        return externalRequestor;
    } else if (if_name == "internal_responder") {
        panic_if(idx != InvalidPortID, "external_resp is not a vector port");
        return internalResponder;
    } else if (if_name == "internal_requestor") {
        panic_if(idx != InvalidPortID, "external_resp is not a vector port");
        return internalRequestor;
    }
    else {
        // pass it along to our super class

        return ClockedObject::getPort(if_name, idx);
    }
}

AddrRangeList
InterconnectLogic::DisaggregatedResponsePort::getAddrRanges() const
{
    AddrRangeList ranges;
    if (owner->range.size()!=0)
        ranges.push_back(owner->range);
    return ranges;
}

void
InterconnectLogic::checkTicket(PacketPtr pkt){
}

bool
InterconnectLogic::DisaggregatedResponsePort::trySendRetry()
{
    if (needRetry ) {
        needRetry = false;
        sendRetryReq();
        return true;
    }

    return false;
}


void
InterconnectLogic::DisaggregatedResponsePort::recvFunctional(PacketPtr pkt)
{
    if (isExternal)
    {
        owner->processReceive(pkt);

        owner->internalRequestor.sendFunctional(pkt);
    }else
    {
        owner->processSend(pkt);

        owner->externalRequestor.sendFunctional(pkt);
    }

}


bool
InterconnectLogic::DisaggregatedResponsePort::recvTimingReq(PacketPtr pkt)
{

    if (isExternal)
    {
        if (owner->isRcvFull())
        {
            needRetry = true;
            return false;
        }

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;


        for(DeferredPacket& dpkt: owner->rcvProcessingQueue){
            if(dpkt.availTick == curTick() + receive_delay)
            {
                inform("%u |for rcv-Req | %u | %p %s %u %u %u %u | %p %u %u %u %u", curTick(), getCID() , dpkt.pkt, dpkt.pkt->isRequest()?"Req":"Res", dpkt.availTick, dpkt.delay, dpkt.pkt->getSrcCID(), dpkt.pkt->getDstCID(), pkt, curTick() + receive_delay, receive_delay, pkt->getSrcCID(), pkt->getDstCID());
            } else if(dpkt.availTick > curTick() + receive_delay)
            {
                inform("for rcv-Req | %u | less %p %s %u %u %u %u | %p %u %u %u %u", getCID() , dpkt.pkt, dpkt.pkt->isRequest()?"Req":"Res", dpkt.availTick, dpkt.delay, dpkt.pkt->getSrcCID(), dpkt.pkt->getDstCID(), pkt, curTick() + receive_delay, receive_delay, pkt->getSrcCID(), pkt->getDstCID());
            }
        }

        owner->rcvProcessingQueue.emplace_back(
            pkt,
            curTick() + receive_delay,
            receive_delay
        );

        owner->schedule(
            new EventFunctionWrapper(
                [this, pkt]{ owner->processReceive();},
                owner->name() + ".processReceive",
                true
            ),
            curTick() + receive_delay
        );
        return true;

    }
    else
    {

        if (owner->isSndFull())
        {
            needRetry = true;
            return false;
        }

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        // Interconnect side should take care of the next delay
        pkt->headerDelay = pkt->payloadDelay = 0;

        for(DeferredPacket& dpkt: owner->sndProcessingQueue){
            if(dpkt.availTick == curTick() + receive_delay)
            {
                inform(" %u |for snd-Req | %u | %p %s %u %u %u %u | %p %u %u %u %u", curTick(),getCID(), dpkt.pkt, dpkt.pkt->isRequest()?"Req":"Res",dpkt.availTick, dpkt.delay, dpkt.pkt->getSrcCID(), dpkt.pkt->getDstCID() , pkt, curTick() + receive_delay, receive_delay, pkt->getSrcCID(), pkt->getDstCID());
            } else if(dpkt.availTick > curTick() + receive_delay)
            {
                inform("for snd-Req | %u | less %p %s %u %u %u %u | %p %u %u %u %u",getCID(), dpkt.pkt, dpkt.pkt->isRequest()?"Req":"Res", dpkt.availTick, dpkt.delay, dpkt.pkt->getSrcCID(), dpkt.pkt->getDstCID(), pkt, curTick() + receive_delay, receive_delay, pkt->getSrcCID(), pkt->getDstCID());
            }
        }

        owner->sndProcessingQueue.emplace_back(
            pkt,
            curTick() + receive_delay,
            receive_delay
        );


        owner->schedule(
            new EventFunctionWrapper(
                [this, pkt]{ owner->processSend(); },
                owner->name() + ".processSend",
                true
            ),
            curTick() + receive_delay
        );
        return true;


    }
    

}


void
InterconnectLogic::DisaggregatedResponsePort::recvRespRetry()
{
    if (isExternal) {
        owner->sndWait = false;
        owner->completeSend();
    }
    else {
        owner->rcvWait = false;
        owner->completeReceive();
    }
}


bool
InterconnectLogic::DisaggregatedRequestPort::trySendRetry()
{
    if (needRetry ) {
        needRetry = false;
        sendRetryResp();
        return true;
    }

    return false;
}


bool
InterconnectLogic::DisaggregatedRequestPort::recvTimingResp(PacketPtr pkt)
{
    if (isExternal)
    {
        if (owner->isRcvFull())
        {
            needRetry = true;
            return false;
        }

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        // Interconnect side should take care of the next delay
        pkt->headerDelay = pkt->payloadDelay = 0;

        for(DeferredPacket& dpkt: owner->rcvProcessingQueue){
            if(dpkt.availTick == curTick() + receive_delay)
            {
                inform("for rcv-Resp | %u | %p %s %u %u %u %u | %p %u %u %u %u", getCID() , dpkt.pkt, dpkt.pkt->isRequest()?"Req":"Res", dpkt.availTick, dpkt.delay, dpkt.pkt->getSrcCID(), dpkt.pkt->getDstCID(), pkt, curTick() + receive_delay, receive_delay, pkt->getSrcCID(), pkt->getDstCID());
            } else if(dpkt.availTick > curTick() + receive_delay)
            {
                inform("for rcv-Resp | %u | less %p %s %u %u %u %u | %p %u %u %u %u", getCID() , dpkt.pkt, dpkt.pkt->isRequest()?"Req":"Res", dpkt.availTick, dpkt.delay, dpkt.pkt->getSrcCID(), dpkt.pkt->getDstCID(), pkt, curTick() + receive_delay, receive_delay, pkt->getSrcCID(), pkt->getDstCID());
            }
        }



        owner->rcvProcessingQueue.emplace_back(
            pkt,
            curTick() + receive_delay,
            receive_delay
        );

        owner->schedule(
            new EventFunctionWrapper(
                [this]{ owner->processReceive(); },
                owner->name() + ".processReceive",
                true
            ),
            curTick() + receive_delay
        );

        return true;

    }
    else
    {
        if (owner->isSndFull())
        {
            needRetry = true;
            return false;
        }

        Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
        // Interconnect side should take care of the next delay
        pkt->headerDelay = pkt->payloadDelay = 0;

        for(DeferredPacket& dpkt: owner->sndProcessingQueue){
            if(dpkt.availTick == curTick() + receive_delay)
            {
                inform("for snd-Resp | %u | %p %s %u %u %u %u | %p %u %u %u %u", getCID() , dpkt.pkt, dpkt.pkt->isRequest()?"Req":"Res", dpkt.availTick, dpkt.delay, dpkt.pkt->getSrcCID(), dpkt.pkt->getDstCID(), pkt, curTick() + receive_delay, receive_delay, pkt->getSrcCID(), pkt->getDstCID());
            } else if(dpkt.availTick > curTick() + receive_delay)
            {
                inform("for snd-Resp | %u | less %p %s %u %u %u %u | %p %u %u %u %u", getCID() , dpkt.pkt, dpkt.pkt->isRequest()?"Req":"Res", dpkt.availTick, dpkt.delay, dpkt.pkt->getSrcCID(), dpkt.pkt->getDstCID(), pkt, curTick() + receive_delay, receive_delay, pkt->getSrcCID(), pkt->getDstCID());
            }
        }

        owner->sndProcessingQueue.emplace_back(
            pkt,
            curTick() + receive_delay,
            receive_delay
        );

        owner->schedule(
            new EventFunctionWrapper(
                [this]{ owner->processSend(); },
                owner->name() + ".processSend",
                true
            ),
            curTick() + receive_delay
        );
        return true;


    }
}


void
InterconnectLogic::DisaggregatedRequestPort::recvReqRetry()
{
    if (isExternal) {
        owner->sndWait = false;
        owner->completeSend();
    }
    else {
        owner->rcvWait = false;
        owner->completeReceive();
    }
}

void
InterconnectLogic::processReceive()
{
    // nothing to process yet so return
    if(rcvProcessingQueue.empty())
        return;

    PacketPtr targetPkt = rcvProcessingQueue.front().pkt;
    Tick avail = rcvProcessingQueue.front().availTick;

    // should have been scheduled already so return
    if(avail > curTick())
        return;

    rcvProcessingQueue.pop_front();

    Tick processAvailTick = acquireAvailableRcvProcessingUnit(latency);
    processReceive(targetPkt);
    rcvReadyQueue.emplace_back(targetPkt, processAvailTick, latency);
    assert(processAvailTick > curTick());

    schedule(
        new EventFunctionWrapper(
            [this]{ 
                completeReceive();
            },
            name() + ".completeReceive",
            true
        ),
       processAvailTick
    );
}

void
InterconnectLogic::processReceive(PacketPtr pkt)
{
    /*
        - receive req
            - translation required if different address space
        - receive resp
            - no translation required => just restore the original paddr 
    */

    if (pkt->isRequest()){

        // if request, the address could be interpreted differently in this node, so translate if needed
        if(isRequireReceiveTranslation)
            translationInformation->recvTranslate(pkt);

    } else{

        // restore the original (in node address space) pkt->addr & pkt->req->_paddr
        pkt->restoreAddr();
        pkt->req->restorePaddr();

    }
    return;
}

void
InterconnectLogic::completeReceive()
{
    if (rcvReadyQueue.empty())
        return;

    PacketPtr targetPkt = rcvReadyQueue.front().pkt;
    Tick avail = rcvReadyQueue.front().availTick;
    // should have been scheduled already so return
    if(avail > curTick()){
        return;
    }

    // waiting for retry request from peer
    // it will be called again after the retry is completed
    if(rcvWait){
        return;
    }

    if (targetPkt->isResponse()){
        if (internalResponder.sendTimingResp(targetPkt)){
            rcvReadyQueue.pop_front();
            numRX++;
            externalResponder.trySendRetry() || externalResponder.trySendRetry(); 
        } else{
            // inform("InterconnectLogic::completeReceive() isResponse");
            rcvWait = true;
            return;
        }
    }
    else {
        if (internalRequestor.sendTimingReq(targetPkt)){
            rcvReadyQueue.pop_front();
            numRX++;
            externalResponder.trySendRetry() || externalResponder.trySendRetry(); 
        } else{
            // inform("InterconnectLogic::completeReceive() !isResponse");
            rcvWait = true;
            return;
        }
    }

    // it might be a call from retry function, in that case, rescheduling completeSend/completeReceive is required
    if (rcvReadyQueue.size())
    {
        schedule(
            new EventFunctionWrapper(
                [this]{ completeReceive(); },
                name() + ".completeReceive",
                true
            ),
            rcvReadyQueue.front().availTick > curTick()? rcvReadyQueue.front().availTick: curTick()
        );
    }
}


void
InterconnectLogic::processSend()
{
    // nothing to process yet so return
    if(sndProcessingQueue.empty())
        return;

    PacketPtr targetPkt = sndProcessingQueue.front().pkt;
    Tick avail = sndProcessingQueue.front().availTick;

    // check if the packet receival is completed
    // if not, should have already been scheduled as a future event so just return
    if(avail > curTick())
        return;

    sndProcessingQueue.pop_front();

    Tick processAvailTick = acquireAvailableSndProcessingUnit(latency);
    processSend(targetPkt);
    sndReadyQueue.emplace_back(targetPkt, processAvailTick, latency);
    assert(processAvailTick > curTick());


    schedule(
        new EventFunctionWrapper(
            [this]{ 
                completeSend();
            },
            name() + ".completeSend",
            true
        ),
       processAvailTick
    );

}



void
InterconnectLogic::processSend(PacketPtr pkt)
{

    /*
        - send req
            - save the original paddr + translation required if different address space
        - send resp
            - no translation required => just send
    */

    if (pkt->isRequest())
    {
        // save pkt->addr & pkt->req->_paddr
        pkt->saveAddr();
        pkt->req->savePaddr();
        pkt->setSrcCID(getCID());

        if (isRequireSendTranslation){
            translationInformation->sendTranslate(pkt);
        }

    }

    return;
}

void
InterconnectLogic::completeSend()
{
    if (sndReadyQueue.empty())
        return;
    
    PacketPtr targetPkt = sndReadyQueue.front().pkt;
    Tick avail = sndReadyQueue.front().availTick;
    // should have been scheduled already so return
    if(avail > curTick()){
        return;
    }

    // waiting for retry request from peer
    // it will be called again after the retry is completed
    if(sndWait){
        return;
    }

    if (targetPkt->isResponse()){
        if (externalResponder.sendTimingResp(targetPkt)){
            sndReadyQueue.pop_front();
            numTX++;
            internalResponder.trySendRetry() || internalRequestor.trySendRetry();
        } else{
            // inform("InterconnectLogic::completeSend() isResponse");
            sndWait = true;
            return;
        }
    }
    else {
        if (externalRequestor.sendTimingReq(targetPkt)){
            sndReadyQueue.pop_front();
            numTX++;
            internalResponder.trySendRetry() || internalRequestor.trySendRetry();
        } else{
            // inform("InterconnectLogic::completeReceive() isResponse");
            sndWait = true;
            return;
        }
    }

    // it might be a call from retry function, in that case, rescheduling completeSend/completeReceive is required
    if (sndReadyQueue.size())
        schedule(
            new EventFunctionWrapper(
                [this]{ completeSend(); },
                name() + ".completeSend",
                true
            ),
            sndReadyQueue.front().availTick > curTick()? sndReadyQueue.front().availTick: curTick()
        );
}

void
InterconnectLogic::regStats()
{
    // If you don't do this you get errors about uninitialized stats.
    ClockedObject::regStats();

    numTX.name(name() + ".numTX")
        .desc("Number of sent traffics to the disaggregated interconnect");

    numRX.name(name() + ".numRX")
    .desc("Number of received traffics from the disaggregated interconnect");
}

InterconnectLogic*
InterconnectLogicParams::create()
{
    return new InterconnectLogic(this);
}



Tick InterconnectLogic::acquireAvailableRcvProcessingUnit(Tick useTime){
    unsigned minIdx = 0;

    for(unsigned i = 0; i < numRcvProcessingUnit; ++i){
        if(rcvProcessingUnitAvailableTimes[i] <= clockEdge()){
            rcvProcessingUnitAvailableTimes[i] = clockEdge();
            minIdx = i;
            break;
        }
        else if(rcvProcessingUnitAvailableTimes[i] < rcvProcessingUnitAvailableTimes[minIdx]){
            minIdx = i;
        }
    }

    rcvProcessingUnitAvailableTimes[minIdx] += useTime;

    return rcvProcessingUnitAvailableTimes[minIdx];
}

Tick InterconnectLogic::acquireAvailableSndProcessingUnit(Tick useTime){
    unsigned minIdx = 0;

    for(unsigned i = 0; i < numSndProcessingUnit; ++i){
        if(sndProcessingUnitAvailableTimes[i] <= clockEdge()){
            sndProcessingUnitAvailableTimes[i] = clockEdge();
            minIdx = i;
            break;
        }
        else if(sndProcessingUnitAvailableTimes[i] < sndProcessingUnitAvailableTimes[minIdx]){
            minIdx = i;
        }
    }

    sndProcessingUnitAvailableTimes[minIdx] += useTime;

    return sndProcessingUnitAvailableTimes[minIdx];
}


void
InterconnectLogic::init()
{
    if (internalResponder.isConnected())
        internalResponder.sendRangeChange();
    if (externalResponder.isConnected())
        externalResponder.sendRangeChange();

    translationInformation =
        module->getSystem()->getTranslationInformation(cid);
}

