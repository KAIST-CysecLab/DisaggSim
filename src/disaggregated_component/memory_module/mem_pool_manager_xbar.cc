/*
 * Copyright (c) 2011-2015, 2018-2020 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2006 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * Definition of a memory pool manager XBar object.
 */

#include "disaggregated_component/memory_module/mem_pool_manager_xbar.hh"

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/AddrRanges.hh"
#include "debug/Drain.hh"
#include "debug/MemPoolManagerXBar.hh"

MemPoolManagerXBar::MemPoolManagerXBar(const MemPoolManagerXBarParams *p)
    : ClockedObject(p),
      frontendLatency(p->frontend_latency),
      forwardLatency(p->forward_latency),
      responseLatency(p->response_latency),
      headerLatency(p->header_latency),
      width(p->width),
      gotAddrRanges(p->port_default_connection_count +
                          p->port_mem_side_ports_connection_count, false),
      gotAllAddrRanges(false), defaultPortID(InvalidPortID),
      transDist(this, "trans_dist", "Transaction distribution"),
      pktCount(this, "pkt_count",
              "Packet count per connected requestor and responder (bytes)"),
      pktSize(this, "pkt_size", "Cumulative packet size per connected "
             "requestor and responder (bytes)")
      // useDefaultRange(p->use_default_range),
{
    /* from noncoherent_xbar.cc */

    // create the ports based on the size of the memory-side port and
    // CPU-side port vector ports, and the presence of the default port,
    // the ports are enumerated starting from zero
    for (int i = 0; i < p->port_mem_side_ports_connection_count; ++i) {
        std::string portName = csprintf("%s.mem_side_port[%d]", name(), i);
        RequestPort* bp = new MemPoolManagerXBarRequestPort(portName, *this, i);
        memSidePorts.push_back(bp);
        reqLayers.push_back(new ReqLayer(*bp, *this,
                                         csprintf("reqLayer%d", i)));

    }

    // see if we have a default CPU-side-port device connected and if so add
    // our corresponding memory-side port
    if (p->port_default_connection_count) {
        defaultPortID = memSidePorts.size();
        std::string portName = name() + ".default";
        RequestPort* bp = new MemPoolManagerXBarRequestPort(portName, *this,
                                                      defaultPortID);
        memSidePorts.push_back(bp);
        reqLayers.push_back(new ReqLayer(*bp, *this, csprintf("reqLayer%d",
                                                              defaultPortID)));
    }

    // create the CPU-side ports, once again starting at zero
    for (int i = 0; i < p->port_cpu_side_ports_connection_count; ++i) {
        std::string portName = csprintf("%s.cpu_side_ports[%d]", name(), i);
        QueuedResponsePort* bp = new MemPoolManagerXBarResponsePort(portName,
                                                                *this, i);
        cpuSidePorts.push_back(bp);
        respLayers.push_back(new RespLayer(*bp, *this,
                                           csprintf("respLayer%d", i)));
    }
}

MemPoolManagerXBar::~MemPoolManagerXBar()
{
    for (auto port: memSidePorts)
        delete port;

    for (auto port: cpuSidePorts)
        delete port;

    /* from noncoherent_xbar.cc */
    for (auto l: reqLayers)
        delete l;
    for (auto l: respLayers)
        delete l;
}

Port &
MemPoolManagerXBar::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "mem_side_ports" && idx < memSidePorts.size()) {
        // the memory-side ports index translates directly to the vector
        // position
        return *memSidePorts[idx];
    } else  if (if_name == "default") {
        return *memSidePorts[defaultPortID];
    } else if (if_name == "cpu_side_ports" && idx < cpuSidePorts.size()) {
        // the CPU-side ports index translates directly to the vector position
        return *cpuSidePorts[idx];
    } else {
        return ClockedObject::getPort(if_name, idx);
    }
}

void
MemPoolManagerXBar::calcPacketTiming(PacketPtr pkt, Tick header_delay)
{
    // the crossbar will be called at a time that is not necessarily
    // coinciding with its own clock, so start by determining how long
    // until the next clock edge (could be zero)
    Tick offset = clockEdge() - curTick();

    // the header delay depends on the path through the crossbar, and
    // we therefore rely on the caller to provide the actual
    // value
    pkt->headerDelay += offset + header_delay;

    // note that we add the header delay to the existing value, and
    // align it to the crossbar clock

    // do a quick sanity check to ensure the timings are not being
    // ignored, note that this specific value may cause problems for
    // slower interconnects
    panic_if(pkt->headerDelay > SimClock::Int::us,
             "Encountered header delay exceeding 1 us\n");

    if (pkt->hasData()) {
        // the payloadDelay takes into account the relative time to
        // deliver the payload of the packet, after the header delay,
        // we take the maximum since the payload delay could already
        // be longer than what this parcitular crossbar enforces.
        pkt->payloadDelay = std::max<Tick>(pkt->payloadDelay,
                                           divCeil(pkt->getSize(), width) *
                                           clockPeriod());
    }

    // the payload delay is not paying for the clock offset as that is
    // already done using the header delay, and the payload delay is
    // also used to determine how long the crossbar layer is busy and
    // thus regulates throughput
}

/* New code: set CID-to-portID mapping */
void
MemPoolManagerXBar::setPortMap(CID subCID, PortID portID)
{
    // panic if the same CID is already mapped
    auto it = portMap.find(subCID);
    if (it != portMap.end()) {
        fatal("The %d subCID tried to be doubly-mapped\n", subCID);
    }

    if(!portMap.insert(std::make_pair(subCID, portID)).second) {
        fatal("The %d subCID failed to be mapped\n", subCID);
    }
}

template <typename SrcType, typename DstType>
MemPoolManagerXBar::Layer<SrcType, DstType>::Layer(
    DstType& _port,
    MemPoolManagerXBar& _xbar,
    const std::string& _name
) :
    Stats::Group(&_xbar, _name.c_str()),
    port(_port), xbar(_xbar), _name(xbar.name() + "." + _name), state(IDLE),
    waitingForPeer(NULL), releaseEvent([this]{ releaseLayer(); }, name()),
    ADD_STAT(occupancy, "Layer occupancy (ticks)"),
    ADD_STAT(utilization, "Layer utilization (%)")
{
    occupancy
        .flags(Stats::nozero);

    utilization
        .precision(1)
        .flags(Stats::nozero);

    utilization = 100 * occupancy / simTicks;
}

template <typename SrcType, typename DstType>
void MemPoolManagerXBar::Layer<SrcType, DstType>::occupyLayer(Tick until)
{
    // ensure the state is busy at this point, as the layer should
    // transition from idle as soon as it has decided to forward the
    // packet to prevent any follow-on calls to sendTiming seeing an
    // unoccupied layer
    assert(state == BUSY);

    // until should never be 0 as express snoops never occupy the layer
    assert(until != 0);
    xbar.schedule(releaseEvent, until);

    // account for the occupied ticks
    occupancy += until - curTick();

    DPRINTF(
        MemPoolManagerXBar,
        "The crossbar layer is now busy from tick %d to %d\n",
        curTick(), until
    );
}

template <typename SrcType, typename DstType>
bool
MemPoolManagerXBar::Layer<SrcType, DstType>::tryTiming(SrcType* src_port)
{
    // if we are in the retry state, we will not see anything but the
    // retrying port (or in the case of the snoop ports the snoop
    // response port that mirrors the actual CPU-side port) as we leave
    // this state again in zero time if the peer does not immediately
    // call the layer when receiving the retry

    // first we see if the layer is busy, next we check if the
    // destination port is already engaged in a transaction waiting
    // for a retry from the peer
    if (state == BUSY || waitingForPeer != NULL) {
        // the port should not be waiting already
        assert(std::find(waitingForLayer.begin(), waitingForLayer.end(),
                         src_port) == waitingForLayer.end());

        // put the port at the end of the retry list waiting for the
        // layer to be freed up (and in the case of a busy peer, for
        // that transaction to go through, and then the layer to free
        // up)
        waitingForLayer.push_back(src_port);
        return false;
    }

    state = BUSY;

    return true;
}

template <typename SrcType, typename DstType>
void
MemPoolManagerXBar::Layer<SrcType, DstType>::succeededTiming(Tick busy_time)
{
    // we should have gone from idle or retry to busy in the tryTiming
    // test
    assert(state == BUSY);

    // occupy the layer accordingly
    occupyLayer(busy_time);
}

template <typename SrcType, typename DstType>
void
MemPoolManagerXBar::Layer<SrcType, DstType>::failedTiming(SrcType* src_port,
                                              Tick busy_time)
{
    // ensure no one got in between and tried to send something to
    // this port
    assert(waitingForPeer == NULL);

    // if the source port is the current retrying one or not, we have
    // failed in forwarding and should track that we are now waiting
    // for the peer to send a retry
    waitingForPeer = src_port;

    // we should have gone from idle or retry to busy in the tryTiming
    // test
    assert(state == BUSY);

    // occupy the bus accordingly
    occupyLayer(busy_time);
}

template <typename SrcType, typename DstType>
void
MemPoolManagerXBar::Layer<SrcType, DstType>::releaseLayer()
{
    // releasing the bus means we should now be idle
    assert(state == BUSY);
    assert(!releaseEvent.scheduled());

    // update the state
    state = IDLE;

    // bus layer is now idle, so if someone is waiting we can retry
    if (!waitingForLayer.empty()) {
        // there is no point in sending a retry if someone is still
        // waiting for the peer
        if (waitingForPeer == NULL)
            retryWaiting();
    } else if (waitingForPeer == NULL && drainState() == DrainState::Draining) {
        DPRINTF(Drain, "Crossbar done draining, signaling drain manager\n");
        //If we weren't able to drain before, do it now.
        signalDrainDone();
    }
}

template <typename SrcType, typename DstType>
void
MemPoolManagerXBar::Layer<SrcType, DstType>::retryWaiting()
{
    // this should never be called with no one waiting
    assert(!waitingForLayer.empty());

    // we always go to retrying from idle
    assert(state == IDLE);

    // update the state
    state = RETRY;

    // set the retrying port to the front of the retry list and pop it
    // off the list
    SrcType* retryingPort = waitingForLayer.front();
    waitingForLayer.pop_front();

    // tell the port to retry, which in some cases ends up calling the
    // layer again
    sendRetry(retryingPort);

    // If the layer is still in the retry state, sendTiming wasn't
    // called in zero time (e.g. the cache does this when a writeback
    // is squashed)
    if (state == RETRY) {
        // update the state to busy and reset the retrying port, we
        // have done our bit and sent the retry
        state = BUSY;

        // occupy the crossbar layer until the next clock edge
        occupyLayer(xbar.clockEdge());
    }
}

template <typename SrcType, typename DstType>
void
MemPoolManagerXBar::Layer<SrcType, DstType>::recvRetry()
{
    // we should never get a retry without having failed to forward
    // something to this port
    assert(waitingForPeer != NULL);

    // add the port where the failed packet originated to the front of
    // the waiting ports for the layer, this allows us to call retry
    // on the port immediately if the crossbar layer is idle
    waitingForLayer.push_front(waitingForPeer);

    // we are no longer waiting for the peer
    waitingForPeer = NULL;

    // if the layer is idle, retry this port straight away, if we
    // are busy, then simply let the port wait for its turn
    if (state == IDLE) {
        retryWaiting();
    } else {
        assert(state == BUSY);
    }
}

/* Old code
 * Should be changed after system-wide CID allocation is implemented */
/*
PortID
MemPoolManagerXBar::findPort(AddrRange addr_range)
{
    ... (skip) ...
}
*/

/* New code
 * find the destination port with memory sub-CID information that is in each
 * Request */
PortID
MemPoolManagerXBar::findPort(PacketPtr pkt)
{
    CID subCID = pkt->req->getMemSubCID();

    auto it = portMap.find(subCID);
    if (it != portMap.end()) {
        return it->second;
    }

    // failed to find corresponding destination CID
    // if defaultPortID is set, use it instead.
    else if (defaultPortID != InvalidPortID) {
        inform("Unable to find portID for subCID %d\n", subCID);
        inform("instead, will use default port %d\n", defaultPortID);
        return defaultPortID;
    }

    // no available port exists
    fatal("Unable to find destination for subCID %d on %s\n", subCID,
            name());
}

/* Function called by the port when the crossbar is receiving a range change.*/
/* Old code: managing address ranges */
/*
void
MemPoolManagerXBar::recvRangeChange(PortID mem_side_port_id)
{
    ... (skip) ...
}
*/

/* New code: no address range managing, but instead CID mapping */
void
MemPoolManagerXBar::recvRangeChange(PortID mem_side_port_id)
{
    DPRINTF(AddrRanges, "Received range change from cpu_side_ports %s\n",
            memSidePorts[mem_side_port_id]->getPeer());

    // remember that we got a range from this memory-side port and thus the
    // connected CPU-side-port module
    gotAddrRanges[mem_side_port_id] = true;

    // initialization: receive CID and update CID-to-PortID mapping
    // using the previous recvRangeChange function
    if (!gotAllAddrRanges) {
        AddrRangeList ranges = memSidePorts[mem_side_port_id]->
                               getAddrRanges();

        // update the global flag
        // take a logical AND of all the ports and see if we got
        // ranges from everyone
        gotAllAddrRanges = true;
        std::vector<bool>::const_iterator r = gotAddrRanges.begin();
        while (gotAllAddrRanges &&  r != gotAddrRanges.end()) {
            gotAllAddrRanges &= *r++;
        }
        if (gotAllAddrRanges)
            DPRINTF(AddrRanges, "Got address ranges from all responders\n");
    }

    // print out simple debug information after receiving all address ranges
    // address range management is not necessary, when packets are routed by
    // their CIDs
    if (gotAllAddrRanges) {
        DPRINTF(AddrRanges, "Received address ragne change after receiving "
                "all address ranges: mem_side_port_id is %d\n",
                mem_side_port_id);

        // tell all our neighbouring memory-side ports that our address
        // ranges have changed
        // TODO: check this whether this causes a problem...
        for (const auto& port: cpuSidePorts)
            port->sendRangeChange();
    }
}

/* Old code: not required since this doesn't manage address ranges */
/*
AddrRangeList
MemPoolManagerXBar::getAddrRanges() const
{
    ... (skip) ...
}
*/

/* from noncoherent_xbar.cc */
// New code: uses subCID for port finding
bool
MemPoolManagerXBar::recvTimingReq(PacketPtr pkt, PortID cpu_side_port_id)
{
    // determine the source port based on the id
    ResponsePort *src_port = cpuSidePorts[cpu_side_port_id];

    // we should never see express snoops on a non-coherent crossbar
    assert(!pkt->isExpressSnoop());

    // determine the destination based on the address
    /* Old code
    PortID mem_side_port_id = findPort(pkt->getAddrRange()); */
    /* New code */
    PortID mem_side_port_id = findPort(pkt);

    // test if the layer should be considered occupied for the current
    // port
    if (!reqLayers[mem_side_port_id]->tryTiming(src_port)) {
        DPRINTF(MemPoolManagerXBar, "recvTimingReq: src %s %s 0x%x BUSY\n",
                src_port->name(), pkt->cmdString(), pkt->getAddr());
        return false;
    }

    DPRINTF(MemPoolManagerXBar, "recvTimingReq: src %s %s 0x%x\n",
            src_port->name(), pkt->cmdString(), pkt->getAddr());

    // store size and command as they might be modified when
    // forwarding the packet
    unsigned int pkt_size = pkt->hasData() ? pkt->getSize() : 0;
    unsigned int pkt_cmd = pkt->cmdToIndex();

    // store the old header delay so we can restore it if needed
    Tick old_header_delay = pkt->headerDelay;

    // a request sees the frontend and forward latency
    Tick xbar_delay = (frontendLatency + forwardLatency) * clockPeriod();

    // set the packet header and payload delay
    calcPacketTiming(pkt, xbar_delay);

    // determine how long to be crossbar layer is busy
    Tick packetFinishTime = clockEdge(Cycles(1)) + pkt->payloadDelay;

    // before forwarding the packet (and possibly altering it),
    // remember if we are expecting a response
    const bool expect_response = pkt->needsResponse() &&
        !pkt->cacheResponding();

    // since it is a normal request, attempt to send the packet
    bool success = memSidePorts[mem_side_port_id]->sendTimingReq(pkt);

    if (!success)  {
        DPRINTF(MemPoolManagerXBar, "recvTimingReq: src %s %s 0x%x RETRY\n",
                src_port->name(), pkt->cmdString(), pkt->getAddr());

        // restore the header delay as it is additive
        pkt->headerDelay = old_header_delay;

        // occupy until the header is sent
        reqLayers[mem_side_port_id]->failedTiming(src_port,
                                                clockEdge(Cycles(1)));

        return false;
    }

    // remember where to route the response to
    if (expect_response) {
        assert(routeTo.find(pkt->req) == routeTo.end());
        routeTo[pkt->req] = cpu_side_port_id;
    }

    reqLayers[mem_side_port_id]->succeededTiming(packetFinishTime);

    // stats updates
    pktCount[cpu_side_port_id][mem_side_port_id]++;
    pktSize[cpu_side_port_id][mem_side_port_id] += pkt_size;
    transDist[pkt_cmd]++;

    return true;
}

/* from noncoherent_xbar.cc */
bool
MemPoolManagerXBar::recvTimingResp(PacketPtr pkt, PortID mem_side_port_id)
{
    // determine the source port based on the id
    RequestPort *src_port = memSidePorts[mem_side_port_id];

    // determine the destination
    const auto route_lookup = routeTo.find(pkt->req);
    assert(route_lookup != routeTo.end());
    const PortID cpu_side_port_id = route_lookup->second;
    assert(cpu_side_port_id != InvalidPortID);
    assert(cpu_side_port_id < respLayers.size());

    // test if the layer should be considered occupied for the current
    // port
    if (!respLayers[cpu_side_port_id]->tryTiming(src_port)) {
        DPRINTF(MemPoolManagerXBar, "recvTimingResp: src %s %s 0x%x BUSY\n",
                src_port->name(), pkt->cmdString(), pkt->getAddr());
        return false;
    }

    DPRINTF(MemPoolManagerXBar, "recvTimingResp: src %s %s 0x%x\n",
            src_port->name(), pkt->cmdString(), pkt->getAddr());

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

    // send the packet through the destination CPU-side port, and pay for
    // any outstanding latency
    Tick latency = pkt->headerDelay;
    pkt->headerDelay = 0;
    cpuSidePorts[cpu_side_port_id]->schedTimingResp(pkt,
                                        curTick() + latency);

    // remove the request from the routing table
    routeTo.erase(route_lookup);

    respLayers[cpu_side_port_id]->succeededTiming(packetFinishTime);

    // stats updates
    pktCount[cpu_side_port_id][mem_side_port_id]++;
    pktSize[cpu_side_port_id][mem_side_port_id] += pkt_size;
    transDist[pkt_cmd]++;

    return true;
}

/* from noncoherent_xbar.cc */
void
MemPoolManagerXBar::recvReqRetry(PortID mem_side_port_id)
{
    // responses never block on forwarding them, so the retry will
    // always be coming from a port to which we tried to forward a
    // request
    reqLayers[mem_side_port_id]->recvRetry();
}

/* from noncoherent_xbar.cc */
// New code: uses subCID for port finding
Tick
MemPoolManagerXBar::recvAtomicBackdoor(PacketPtr pkt, PortID cpu_side_port_id,
                                    MemBackdoorPtr *backdoor)
{
    DPRINTF(MemPoolManagerXBar, "recvAtomic: packet src %s addr 0x%x cmd %s\n",
            cpuSidePorts[cpu_side_port_id]->name(), pkt->getAddr(),
            pkt->cmdString());

    unsigned int pkt_size = pkt->hasData() ? pkt->getSize() : 0;
    unsigned int pkt_cmd = pkt->cmdToIndex();

    // determine the destination port
    /* Old code
    PortID mem_side_port_id = findPort(pkt->getAddrRange()); */
    /* New code */
    PortID mem_side_port_id = findPort(pkt);

    // stats updates for the request
    pktCount[cpu_side_port_id][mem_side_port_id]++;
    pktSize[cpu_side_port_id][mem_side_port_id] += pkt_size;
    transDist[pkt_cmd]++;

    // forward the request to the appropriate destination
    auto mem_side_port = memSidePorts[mem_side_port_id];
    Tick response_latency = backdoor ?
        mem_side_port->sendAtomicBackdoor(pkt, *backdoor) :
        mem_side_port->sendAtomic(pkt);

    // add the response data
    if (pkt->isResponse()) {
        pkt_size = pkt->hasData() ? pkt->getSize() : 0;
        pkt_cmd = pkt->cmdToIndex();

        // stats updates
        pktCount[cpu_side_port_id][mem_side_port_id]++;
        pktSize[cpu_side_port_id][mem_side_port_id] += pkt_size;
        transDist[pkt_cmd]++;
    }

    // @todo: Not setting first-word time
    pkt->payloadDelay = response_latency;
    return response_latency;
}

/* from noncoherent_xbar.cc */
// New code: uses subCID for port finding
void
MemPoolManagerXBar::recvFunctional(PacketPtr pkt, PortID cpu_side_port_id)
{
    if (!pkt->isPrint()) {
        // don't do DPRINTFs on PrintReq as it clutters up the output
        DPRINTF(MemPoolManagerXBar,
                "recvFunctional: packet src %s addr 0x%x cmd %s\n",
                cpuSidePorts[cpu_side_port_id]->name(), pkt->getAddr(),
                pkt->cmdString());
    }

    // since our CPU-side ports are queued ports we need to check them as well
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
    /* Old code
    PortID dest_id = findPort(pkt->getAddrRange()); */
    /* New code */
    PortID dest_id = findPort(pkt);

    // forward the request to the appropriate destination
    memSidePorts[dest_id]->sendFunctional(pkt);
}

void
MemPoolManagerXBar::regStats()
{
    ClockedObject::regStats();

    using namespace Stats;

    transDist
        .init(MemCmd::NUM_MEM_CMDS)
        .flags(nozero);

    // get the string representation of the commands
    for (int i = 0; i < MemCmd::NUM_MEM_CMDS; i++) {
        MemCmd cmd(i);
        const std::string &cstr = cmd.toString();
        transDist.subname(i, cstr);
    }

    pktCount
        .init(cpuSidePorts.size(), memSidePorts.size())
        .flags(total | nozero | nonan);

    pktSize
        .init(cpuSidePorts.size(), memSidePorts.size())
        .flags(total | nozero | nonan);

    // both the packet count and total size are two-dimensional
    // vectors, indexed by CPU-side port id and memory-side port id, thus the
    // neighbouring memory-side ports and CPU-side ports, they do not
    // differentiate what came from the memory-side ports and was forwarded to
    // the CPU-side ports (requests and snoop responses) and what came from
    // the CPU-side ports and was forwarded to the memory-side ports (responses
    // and snoop requests)
    for (int i = 0; i < cpuSidePorts.size(); i++) {
        pktCount.subname(i, cpuSidePorts[i]->getPeer().name());
        pktSize.subname(i, cpuSidePorts[i]->getPeer().name());
        for (int j = 0; j < memSidePorts.size(); j++) {
            pktCount.ysubname(j, memSidePorts[j]->getPeer().name());
            pktSize.ysubname(j, memSidePorts[j]->getPeer().name());
        }
    }
}

template <typename SrcType, typename DstType>
DrainState
MemPoolManagerXBar::Layer<SrcType, DstType>::drain()
{
    //We should check that we're not "doing" anything, and that noone is
    //waiting. We might be idle but have someone waiting if the device we
    //contacted for a retry didn't actually retry.
    if (state != IDLE) {
        DPRINTF(Drain, "Crossbar not drained\n");
        return DrainState::Draining;
    } else {
        return DrainState::Drained;
    }
}

/**
 * Crossbar layer template instantiations. Could be removed with _impl.hh
 * file, but since there are only two given options (RequestPort and
 * ResponsePort) it seems a bit excessive at this point.
 */
template class MemPoolManagerXBar::Layer<ResponsePort, RequestPort>;
template class MemPoolManagerXBar::Layer<RequestPort, ResponsePort>;

MemPoolManagerXBar*
MemPoolManagerXBarParams::create()
{
    return new MemPoolManagerXBar(this);
}
