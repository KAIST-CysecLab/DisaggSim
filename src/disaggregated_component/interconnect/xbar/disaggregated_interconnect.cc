#include "disaggregated_component/interconnect/xbar/disaggregated_interconnect.hh"

#include <typeinfo>

#include "debug/DisaggregatedInterconnect.hh"
#include "disaggregated_component/interconnect_logic.hh"

DisaggregatedInterconnect::DisaggregatedInterconnect(Params *p):
    BaseXBar(p),
    cidToPortIDMap(p->port_mem_side_ports_connection_count,-1)
{
    // create the ports based on the size of the requestor and resonder
    // vector ports, and the presence of the default port, the ports
    // are enumerated starting from zero
    for (int i = 0; i < p->port_mem_side_ports_connection_count; ++i) {
        std::string portName = csprintf("%s.mem_side_port[%d]", name(), i);
        RequestPort* bp = new DisaggregatedInterconnectRequestPort(
                            portName,
                            *this,
                            i
                        );
        memSidePorts.push_back(bp);
        reqLayers.push_back(new ReqLayer(*bp, *this,
                                         csprintf("reqLayer%d", i)));
    }

    // see if we have a default response device connected and if so add
    // our corresponding requestor port
    if (p->port_default_connection_count) {
        defaultPortID = memSidePorts.size();
        std::string portName = name() + ".default";
        RequestPort* bp = new DisaggregatedInterconnectRequestPort(
                            portName,
                            *this,
                            defaultPortID
                        );
        memSidePorts.push_back(bp);
        reqLayers.push_back(new ReqLayer(*bp, *this, csprintf("reqLayer%d",
                                                              defaultPortID)));
    }

    // create the response ports, once again starting at zero
    for (int i = 0; i < p->port_cpu_side_ports_connection_count; ++i) {
        std::string portName = csprintf("%s.cpu_side_port[%d]", name(), i);
        QueuedResponsePort* bp = new DisaggregatedInterconnectResponsePort(
                                    portName,
                                    *this,
                                    i
                                );
        cpuSidePorts.push_back(bp);
        respLayers.push_back(new RespLayer(*bp, *this,
                                           csprintf("respLayer%d", i)));
    }
}

DisaggregatedInterconnect::~DisaggregatedInterconnect()
{
    for (auto l: reqLayers)
        delete l;
    for (auto l: respLayers)
        delete l;
}


PortID DisaggregatedInterconnect::findPort(uint32_t dst_cid)
{
    return cidToPortIDMap[dst_cid];
}


void DisaggregatedInterconnect::recvRangeChange(PortID request_port_id)
{
    // remember that we got a range from this requestor port and thus the
    // connected response module
    gotAddrRanges[request_port_id] = true;

    // update the global flag
    if (!gotAllAddrRanges) {
        // take a logical AND of all the ports and see if we got
        // ranges from everyone
        gotAllAddrRanges = true;
        std::vector<bool>::const_iterator r = gotAddrRanges.begin();
        while (gotAllAddrRanges &&  r != gotAddrRanges.end()) {
            gotAllAddrRanges &= *r++;
        }
    }


    if (request_port_id != defaultPortID) {

        try
        {
            InterconnectLogic::DisaggregatedResponsePort& logicPort =
                dynamic_cast<InterconnectLogic::DisaggregatedResponsePort&>(
                    memSidePorts[request_port_id]->getPeer()
                );

            cidToPortIDMap[logicPort.getCID()] = request_port_id;

        } catch(std::bad_cast& bc)
        {
            fatal(
                "non-disaggregated logic port at disaggregated interconnect"
            );
        }
    }


}



bool
DisaggregatedInterconnect::recvTimingReq(PacketPtr pkt, PortID response_port_id)
{
    // determine the source port based on the id
    ResponsePort *src_port = cpuSidePorts[response_port_id];
    DPRINTF(DisaggregatedInterconnect, "recvTimingReq: src %s %s 0x%x BUSY\n",
                src_port->name(), pkt->cmdString().c_str(), pkt->getAddr());
    // we should never see express snoops on a non-coherent crossbar
    assert(!pkt->isExpressSnoop());

    // determine the destination based on the address
    PortID request_port_id = findPort(pkt->req->getDstCID());

    // test if the layer should be considered occupied for the current
    // port
    if (!reqLayers[request_port_id]->tryTiming(src_port)) {
        return false;
    }
    // store size and command as they might be modified when
    // forwarding the packet
    unsigned int pkt_size = pkt->hasData() ? pkt->getSize() : 0;
    unsigned int pkt_cmd = pkt->cmdToIndex();

    // store the old header delay so we can restore it if needed
    Tick old_header_delay = pkt->headerDelay;

    // a request sees the frontend and forward latency
    // decode and arbitration : frontendLatency
    // latency between sending and decode/arbitration complete : forwardLatency
    Tick xbar_delay = (frontendLatency + forwardLatency) * clockPeriod();

    // set the packet header and payload delay
    calcPacketTiming(pkt, xbar_delay);

    // before forwarding the packet (and possibly altering it),
    // remember if we are expecting a response
    const bool expect_response = pkt->needsResponse() &&
        !pkt->cacheResponding();

    // since it is a normal request, attempt to send the packet
    bool success = memSidePorts[request_port_id]->sendTimingReq(pkt);

    if (!success)  {

        // restore the header delay as it is additive
        pkt->headerDelay = old_header_delay;

        // occupy until the header is sent
        reqLayers[request_port_id]->failedTiming(src_port,
                                                clockEdge(Cycles(1)));

        return false;
    }

    // remember where to route the response to
    if (expect_response) {
        assert(routeTo.find(pkt->req) == routeTo.end());
        routeTo[pkt->req] = response_port_id;
    }

    // determine how long to be crossbar layer is busy
    Tick packetFinishTime = clockEdge(Cycles(1)) + pkt->payloadDelay;

    reqLayers[request_port_id]->succeededTiming(packetFinishTime);

    // stats updates
    pktCount[response_port_id][request_port_id]++;
    pktSize[response_port_id][request_port_id] += pkt_size;
    transDist[pkt_cmd]++;

    return true;
}


bool
DisaggregatedInterconnect::recvTimingResp(PacketPtr pkt, PortID request_port_id)
{
    // determine the source port based on the id
    RequestPort *src_port = memSidePorts[request_port_id];
    DPRINTF(DisaggregatedInterconnect, "recvTimingResp: src %s %s 0x%x BUSY\n",
                src_port->name(), pkt->cmdString(), pkt->getAddr());
    // determine the destination
    const auto route_lookup = routeTo.find(pkt->req);
    assert(route_lookup != routeTo.end());
    const PortID response_port_id = route_lookup->second;
    assert(response_port_id != InvalidPortID);
    assert(response_port_id < respLayers.size());

    // test if the layer should be considered occupied for the current
    // port
    if (!respLayers[response_port_id]->tryTiming(src_port)) {
        return false;
    }

    // store size and command as they might be modified when
    // forwarding the packet
    unsigned int pkt_size = pkt->hasData() ? pkt->getSize() : 0;
    unsigned int pkt_cmd = pkt->cmdToIndex();

    // a response sees the response latency
    Tick xbar_delay = responseLatency * clockPeriod();

    // set the packet header and payload delay
    calcPacketTiming(pkt, xbar_delay);

    // determine how long to be crossbar layer is busy
    Tick packetFinishTime = clockEdge(Cycles(1)) + pkt->payloadDelay;

    // send the packet through the destination response port, and pay for
    // any outstanding latency
    Tick latency = pkt->headerDelay;
    pkt->headerDelay = 0;
    cpuSidePorts[response_port_id]->schedTimingResp(pkt, curTick() + latency);

    // remove the request from the routing table
    routeTo.erase(route_lookup);

    respLayers[response_port_id]->succeededTiming(packetFinishTime);

    // stats updates
    pktCount[response_port_id][request_port_id]++;
    pktSize[response_port_id][request_port_id] += pkt_size;
    transDist[pkt_cmd]++;

    return true;
}

void
DisaggregatedInterconnect::recvReqRetry(PortID request_port_id)
{
    // responses never block on forwarding them, so the retry will
    // always be coming from a port to which we tried to forward a
    // request
    reqLayers[request_port_id]->recvRetry();
}


Tick
DisaggregatedInterconnect::recvAtomicBackdoor(
    PacketPtr pkt, PortID response_port_id,
    MemBackdoorPtr *backdoor
)
{
    unsigned int pkt_size = pkt->hasData() ? pkt->getSize() : 0;
    unsigned int pkt_cmd = pkt->cmdToIndex();

    // determine the destination port
    PortID request_port_id = findPort(pkt->req->getDstCID());

    // stats updates for the request
    pktCount[response_port_id][request_port_id]++;
    pktSize[response_port_id][request_port_id] += pkt_size;
    transDist[pkt_cmd]++;

    // forward the request to the appropriate destination
    auto requestor = memSidePorts[request_port_id];
    Tick response_latency = backdoor ?
        requestor->sendAtomicBackdoor(pkt, *backdoor) : requestor->sendAtomic(pkt);

    // add the response data
    if (pkt->isResponse()) {
        pkt_size = pkt->hasData() ? pkt->getSize() : 0;
        pkt_cmd = pkt->cmdToIndex();

        // stats updates
        pktCount[response_port_id][request_port_id]++;
        pktSize[response_port_id][request_port_id] += pkt_size;
        transDist[pkt_cmd]++;
    }

    // @todo: Not setting first-word time
    pkt->payloadDelay = response_latency;
    return response_latency;
}

void
DisaggregatedInterconnect::recvFunctional(PacketPtr pkt, PortID response_port_id)
{
    if (!pkt->isPrint()) {
        // don't do DPRINTFs on PrintReq as it clutters up the output
    }

    // since our response ports are queued ports we need to check them as well
    for (const auto& p : cpuSidePorts) {
        // if we find a response that has the data, then the
        // downstream caches/memories may be out of date, so simply stop
        // here
        if (p->trySatisfyFunctional(pkt)) {
            if (pkt->needsResponse())
                pkt->makeResponse();
            return;
        }
    }

    // determine the destination port
    PortID dest_id = findPort(pkt->req->getDstCID());

    // forward the request to the appropriate destination
    memSidePorts[dest_id]->sendFunctional(pkt);
}


DisaggregatedInterconnect*
DisaggregatedInterconnectParams::create()
{
    return new DisaggregatedInterconnect(this);
}
