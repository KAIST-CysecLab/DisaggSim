/*
 * Copyright (c) 2010-2020 ARM Limited
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
 * Copyright (c) 2013 Amin Farmahini-Farahani
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

#include "disaggregated_component/memory_module/mem_pool_ctrl.hh"

#include "base/trace.hh"
#include "debug/DRAM.hh"
#include "debug/Drain.hh"
// #include "debug/MemCtrl.hh"
#include "debug/MemPoolCtrl.hh"
#include "debug/NVM.hh"
#include "debug/QOS.hh"
#include "mem/mem_interface.hh"
#include "sim/system.hh"

/*
 * NOTE: Many functions rely on MemPacket's isDram() and call
 * DisagDRAMInterface's member methods. To support multiple media,
 * now they use new member methods getDram()/getNvm().
 * See the MemPoolCtrlPacket class's getDram()/getNvm() in mem_pool_ctrl.hh.
 *
 * Related source code: mem/mem_interface.{hh,cc}
 */

using namespace std;

// New functions for support multiple media
DisagDRAMInterface* MemPoolCtrlPacket::getDram() {
    return dynamic_cast<DisagDRAMInterface*>(medium);
}
DisagNVMInterface* MemPoolCtrlPacket::getNvm() {
    return dynamic_cast<DisagNVMInterface*>(medium);
}

// New constructor
MemPoolCtrl::MemPoolCtrl(const MemPoolCtrlParams* p) :
    QoS::MemCtrl(p),
    port(name() + ".port", *this), isTimingMode(false),
    retryRdReq(false), retryWrReq(false),
    nextReqEvent([this]{ processNextReqEvent(); }, name()),
    respondEvent([this]{ processRespondEvent(); }, name()),
    minWritesPerSwitch(p->min_writes_per_switch),
    writesThisTime(0), readsThisTime(0),
    memSchedPolicy(p->mem_sched_policy),
    frontendLatency(p->static_frontend_latency),
    backendLatency(p->static_backend_latency),
    commandWindow(p->command_window),
    nextBurstAt(0), prevArrival(0),
    nextReqTime(0),
    stats(*this),
    drams(p->drams), nvms(p->nvms), // New code for disaggregated system
    memSubCID(p->subCID) // New code for disaggregated system
{
    /* Original codes for initialization
    dram(p->dram), nvm(p->nvm),
    readBufferSize((dram ? dram->readBufferSize : 0) +
                   (nvm ? nvm->readBufferSize : 0)),
    writeBufferSize((dram ? dram->writeBufferSize : 0) +
                    (nvm ? nvm->writeBufferSize : 0)),
    writeHighThreshold(writeBufferSize * p->write_high_thresh_perc / 100.0),
    writeLowThreshold(writeBufferSize * p->write_low_thresh_perc / 100.0), */

    /* For now, just use the sum of the all media */
    readBufferSize = 0;
    writeBufferSize = 0;
    if (dramExist()) {
        for (auto it = drams.begin(); it != drams.end(); it++) {
            readBufferSize += (*it)->readBufferSize;
            writeBufferSize += (*it)->writeBufferSize;
        }
    }
    if (nvmExist()) {
        for (auto it = nvms.begin() ; it != nvms.end(); it++) {
            readBufferSize += (*it)->readBufferSize;
            writeBufferSize += (*it)->writeBufferSize;
        }
    }
    writeHighThreshold = (writeBufferSize * p->write_high_thresh_perc / 100.0);
    writeLowThreshold = (writeBufferSize * p->write_low_thresh_perc / 100.0);

    DPRINTF(MemPoolCtrl, "Setting up controller\n");
    readQueue.resize(p->qos_priorities);
    writeQueue.resize(p->qos_priorities);

    // Hook up interfaces to the controller
    /* Old code
    if (dram)
        dram->setCtrl(this, commandWindow);
    if (nvm)
        nvm->setCtrl(this, commandWindow); */
    // New code
    if (dramExist())
        for (auto it = drams.begin(); it != drams.end(); it++)
            (*it)->setCtrl(this, commandWindow);
    if (nvmExist())
        for (auto it = nvms.begin(); it != nvms.end(); it++)
            (*it)->setCtrl(this, commandWindow);

    fatal_if(!dramExist() && !nvmExist(),
            "Memory controller must have an interface");

    fatal_if(memSubCID >= 9999,
            "Memory controller must have 0 <= sub CID < 9999");

    // perform a basic check of the write thresholds
    if (p->write_low_thresh_perc >= p->write_high_thresh_perc)
        fatal("Write buffer low threshold %d must be smaller than the "
              "high threshold %d\n", p->write_low_thresh_perc,
              p->write_high_thresh_perc);
}

void
MemPoolCtrl::init()
{
   if (!port.isConnected()) {
        // fatal("MemCtrl %s is unconnected!\n", name()); // Old code
        fatal("MemPoolCtrl %s is unconnected!\n", name()); // New code
    } else {
        port.sendRangeChange();
        // new code: send subCID information to memory pool manager
        port.sendMemSubCID();
    }
}

// New function
void
MemPoolCtrl::startup()
{
    // remember the memory system mode of operation
    isTimingMode = system()->isTimingMode();

    if (isTimingMode) {
        /* shift the bus busy time sufficiently far ahead that we never
         * have to worry about negative values when computing the time for
         * the next request, this will add an insignificant bubble at the
         * start of simulation */

        /* Old code:
           nextBurstAt = curTick() + (dram ? dram->commandOffset() :
           nvm->commandOffset()); */
        /* New code: delaying as much as the sum of all mediae commandOffset
         * value */
        Tick sum = 0;
        if (dramExist()) {
            for (auto it = drams.begin(); it != drams.end(); it++) {
                sum += (*it)->commandOffset();
            }
        }
        if (nvmExist()) {
            for (auto it = nvms.begin(); it != nvms.end(); it++) {
                sum += (*it)->commandOffset();
            }
        }

        nextBurstAt = curTick() + sum;
    }
}

/* new recvAtomic supports multiple media
 * Old function:
Tick
MemPoolCtrl::recvAtomic(PacketPtr pkt)
{
    ... (skip) ...
} */
/* New function:
 * i.e., address matching for media that this MemPoolCtrl has */
Tick
MemPoolCtrl::recvAtomic(PacketPtr pkt)
{
    DPRINTF(MemPoolCtrl, "recvAtomic: %s 0x%x\n",
                     pkt->cmdString(), pkt->getAddr());

    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    Tick latency = 0;
    // do the actual memory access and turn the packet into a response (Atomic)

    // Whether the mediae address range contains the packet's address
    bool pkt_processed = false;
    // DRAM
    if (dramExist()) {
        // check all DRAMs for address matching
        for (auto it = drams.begin(); it != drams.end(); it++) {
            if ((*it)->getAddrRange().contains(pkt->getAddr())) {
                (*it)->access(pkt);

                if (pkt->hasData()) {
                    // this value is not supposed to be accurate, just enough
                    // to keep things going, mimic a closed page
                    latency = (*it)->accessLatency();
                }

                // The other DRAMs won't have the matching address range
                pkt_processed = true;
                break;
            }
        }
    }
    // NVM
    if (!pkt_processed && nvmExist()) {
        // check all NVMs for address matching
        for (auto it = nvms.begin(); it != nvms.end(); it++) {
            if ((*it)->getAddrRange().contains(pkt->getAddr())) {
                (*it)->access(pkt);

                if (pkt->hasData()) {
                    // this value is not supposed to be accurate, just enough
                    // to keep things going, mimic a closed page
                    latency = (*it)->accessLatency();
                }

                // The other DRAMs won't have the matching address range
                pkt_processed = true;
                break;
            }
        }
    }
    // Panic if no medium has the packet's address in their range
    if (!pkt_processed) {
        panic("[MemPoolCtrl::recvAtomic] Can't handle address range for packet %s\n",
                pkt->print());
    }

    return latency;
}

bool
MemPoolCtrl::readQueueFull(unsigned int neededEntries) const
{
    DPRINTF(MemPoolCtrl,
            "Read queue limit %d, current size %d, entries needed %d\n",
            readBufferSize, totalReadQueueSize + respQueue.size(),
            neededEntries);

    auto rdsize_new = totalReadQueueSize + respQueue.size() + neededEntries;
    return rdsize_new > readBufferSize;
}

bool
MemPoolCtrl::writeQueueFull(unsigned int neededEntries) const
{
    DPRINTF(MemPoolCtrl,
            "Write queue limit %d, current size %d, entries needed %d\n",
            writeBufferSize, totalWriteQueueSize, neededEntries);

    auto wrsize_new = (totalWriteQueueSize + neededEntries);
    return  wrsize_new > writeBufferSize;
}

// supports multiple media
// New function
void
MemPoolCtrl::addToReadQueue(PacketPtr pkt, unsigned int pkt_count,
        DisagMemInterface* medium)
{
    // only add to the read queue here. whenever the request is
    // eventually done, set the readyTime, and call schedule()
    assert(!pkt->isWrite());

    assert(pkt_count != 0);

    // if the request size is larger than burst size, the pkt is split into
    // multiple packets
    // Note if the pkt starting address is not aligened to burst size, the
    // address of first packet is kept unaliged. Subsequent packets
    // are aligned to burst size boundaries. This is to ensure we accurately
    // check read packets against packets in write queue.
    const Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;
    unsigned pktsServicedByWrQ = 0;
    BurstHelper* burst_helper = NULL;

    /* Old code
       uint32_t burst_size = is_dram ? dram->bytesPerBurst() :
       nvm->bytesPerBurst();
       */
    // New code
    uint32_t burst_size = medium->bytesPerBurst();

    for (int cnt = 0; cnt < pkt_count; ++cnt) {
        unsigned size = std::min((addr | (burst_size - 1)) + 1,
                base_addr + pkt->getSize()) - addr;
        stats.readPktSize[ceilLog2(size)]++;
        stats.readBursts++;
        stats.requestorReadAccesses[pkt->requestorId()]++;

        // First check write buffer to see if the data is already at
        // the controller
        bool foundInWrQ = false;
        // Addr burst_addr = burstAlign(addr, is_dram); // Old code
        Addr burst_addr = burstAlign(addr, medium); // New code
        // if the burst address is not present then there is no need
        // looking any further
        if (isInWriteQueue.find(burst_addr) != isInWriteQueue.end()) {
            for (const auto& vec : writeQueue) {
                for (const auto& p : vec) {
                    // check if the read is subsumed in the write queue
                    // packet we are looking at
                    if (p->addr <= addr &&
                            ((addr + size) <= (p->addr + p->size))) {

                        foundInWrQ = true;
                        stats.servicedByWrQ++;
                        pktsServicedByWrQ++;
                        DPRINTF(MemPoolCtrl,
                                "Read to addr %lld with size %d serviced by "
                                "write queue\n",
                                addr, size);
                        stats.bytesReadWrQ += burst_size;
                        break;
                    }
                }
            }
        }

        // If not found in the write q, make a memory packet and
        // push it onto the read queue
        if (!foundInWrQ) {

            // Make the burst helper for split packets
            if (pkt_count > 1 && burst_helper == NULL) {
                DPRINTF(MemPoolCtrl, "Read to addr %lld translates to %d "
                        "memory requests\n", pkt->getAddr(), pkt_count);
                burst_helper = new BurstHelper(pkt_count);
            }

            MemPoolCtrlPacket* mem_pkt;
            MemPacket* _decoded_pkt;
            /* NOTE:
             * decodePacket's 5th argument is is_dram
             * It seems that decodePacket doesn't use that argument, but only
             * passes it to MemPacket's constructor
             * => passing the argument, but will NOT use it anymore
             * => Instead, call MemPoolCtrlPacket constructor with the returned
             *    MemPacket* 
             * => see the MemPoolCtrlPacket class in mem_pool_ctrl.hh */
            // if (is_dram)  // Old code
            if (DisagDRAMInterface* pDram =
                    dynamic_cast<DisagDRAMInterface*>(medium)) {
                _decoded_pkt = pDram->decodePacket(pkt, addr, size, true, true);
                mem_pkt = new MemPoolCtrlPacket(_decoded_pkt, medium);
                delete _decoded_pkt;
                // increment read entries of the rank
                pDram->setupRank(mem_pkt->rank, true);
            } else if (DisagNVMInterface* pNvm =
                    dynamic_cast<DisagNVMInterface*>(medium)) {
                _decoded_pkt = pNvm->decodePacket(pkt, addr, size, true, false);
                mem_pkt = new MemPoolCtrlPacket(_decoded_pkt, medium);
                delete _decoded_pkt;
                // Increment count to trigger issue of non-deterministic read
                pNvm->setupRank(mem_pkt->rank, true);
                // Default readyTime to Max; will be reset once read is issued
                mem_pkt->readyTime = MaxTick;
            } else {
                panic("Can't handle medium type for packet %s\n",
                        pkt->print());
            }

            mem_pkt->burstHelper = burst_helper;

            assert(!readQueueFull(1));
            stats.rdQLenPdf[totalReadQueueSize + respQueue.size()]++;

            DPRINTF(MemPoolCtrl, "Adding to read queue\n");

            readQueue[mem_pkt->qosValue()].push_back(mem_pkt);

            // log packet
            logRequest(MemPoolCtrl::READ, pkt->requestorId(), pkt->qosValue(),
                    mem_pkt->addr, 1);

            // Update stats
            stats.avgRdQLen = totalReadQueueSize + respQueue.size();
        }

        // Starting address of next memory pkt (aligned to burst boundary)
        addr = (addr | (burst_size - 1)) + 1;
    }

    // If all packets are serviced by write queue, we send the repsonse back
    if (pktsServicedByWrQ == pkt_count) {
        accessAndRespond(pkt, frontendLatency);
        return;
    }

    // Update how many split packets are serviced by write queue
    if (burst_helper != NULL) {
        burst_helper->burstsServiced = pktsServicedByWrQ;
    }

    // If we are not already scheduled to get a request out of the
    // queue, do so now
    if (!nextReqEvent.scheduled()) {
        DPRINTF(MemPoolCtrl, "Request scheduled immediately\n");
        schedule(nextReqEvent, curTick());
    }
}

// supports multiple media
// New function
void
MemPoolCtrl::addToWriteQueue(PacketPtr pkt, unsigned int pkt_count,
        DisagMemInterface* medium)
{
    // only add to the write queue here. whenever the request is
    // eventually done, set the readyTime, and call schedule()
    assert(pkt->isWrite());

    // if the request size is larger than burst size, the pkt is split into
    // multiple packets
    const Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;

    /* Old code
       uint32_t burst_size = is_dram ? dram->bytesPerBurst() :
       nvm->bytesPerBurst(); */
    // New code
    uint32_t burst_size = medium->bytesPerBurst();

    for (int cnt = 0; cnt < pkt_count; ++cnt) {
        unsigned size = std::min((addr | (burst_size - 1)) + 1,
                base_addr + pkt->getSize()) - addr;
        stats.writePktSize[ceilLog2(size)]++;
        stats.writeBursts++;
        stats.requestorWriteAccesses[pkt->requestorId()]++;

        // see if we can merge with an existing item in the write
        // queue and keep track of whether we have merged or not
        /* Old code
           bool merged = isInWriteQueue.find(burstAlign(addr, is_dram)) !=
           isInWriteQueue.end(); */
        // New code
        bool merged = isInWriteQueue.find(burstAlign(addr, medium)) !=
            isInWriteQueue.end();

        // if the item was not merged we need to create a new write
        // and enqueue it
        if (!merged) {
            MemPoolCtrlPacket* mem_pkt;
            MemPacket* _decoded_pkt;

            /* NOTE:
             * decodePacket's 5th argument is is_dram
             * It seems that decodePacket doesn't use that argument, but only
             * passes it to MemPacket's constructor
             * => passing the argument, but will NOT use it anymore
             * => Instead, call MemPoolCtrlPacket constructor with the returned
             *    MemPacket* 
             * => see the MemPoolCtrlPacket class in mem_pool_ctrl.hh */
            // if (is_dram)  // Old code
            if (DisagDRAMInterface* pDram =
                    dynamic_cast<DisagDRAMInterface*>(medium)) {
                _decoded_pkt = pDram->decodePacket(pkt, addr, size, false, true);
                mem_pkt = new MemPoolCtrlPacket(_decoded_pkt, medium);
                delete _decoded_pkt;
                pDram->setupRank(mem_pkt->rank, false);
            }
            else if (DisagNVMInterface* pNvm =
                    dynamic_cast<DisagNVMInterface*>(medium)) {
                _decoded_pkt = pNvm->decodePacket(pkt, addr, size, false, false);
                mem_pkt = new MemPoolCtrlPacket(_decoded_pkt, medium);
                delete _decoded_pkt;
                pNvm->setupRank(mem_pkt->rank, false);
            }
            else {
                panic("Can't handle medium type for packet %s\n",
                        pkt->print());
            }

            assert(totalWriteQueueSize < writeBufferSize);
            stats.wrQLenPdf[totalWriteQueueSize]++;

            DPRINTF(MemPoolCtrl, "Adding to write queue\n");

            writeQueue[mem_pkt->qosValue()].push_back(mem_pkt);
            // isInWriteQueue.insert(burstAlign(addr, is_dram)); // Old code
            isInWriteQueue.insert(burstAlign(addr, medium)); // New code

            // log packet
            logRequest(MemPoolCtrl::WRITE, pkt->requestorId(), pkt->qosValue(),
                    mem_pkt->addr, 1);

            assert(totalWriteQueueSize == isInWriteQueue.size());

            // Update stats
            stats.avgWrQLen = totalWriteQueueSize;

        } else {
            DPRINTF(MemPoolCtrl,
                    "Merging write burst with existing queue entry\n");

            // keep track of the fact that this burst effectively
            // disappeared as it was merged with an existing one
            stats.mergedWrBursts++;
        }

        // Starting address of next memory pkt (aligned to burst_size boundary)
        addr = (addr | (burst_size - 1)) + 1;
    }

    // we do not wait for the writes to be send to the actual memory,
    // but instead take responsibility for the consistency here and
    // snoop the write queue for any upcoming reads
    // @todo, if a pkt size is larger than burst size, we might need a
    // different front end latency
    accessAndRespond(pkt, frontendLatency);

    // If we are not already scheduled to get a request out of the
    // queue, do so now
    if (!nextReqEvent.scheduled()) {
        DPRINTF(MemPoolCtrl, "Request scheduled immediately\n");
        schedule(nextReqEvent, curTick());
    }
}

void
MemPoolCtrl::printQs() const
{
#if TRACING_ON
    DPRINTF(MemPoolCtrl, "===READ QUEUE===\n\n");
    for (const auto& queue : readQueue) {
        for (const auto& packet : queue) {
            DPRINTF(MemPoolCtrl, "Read %lu\n", packet->addr);
        }
    }

    DPRINTF(MemPoolCtrl, "\n===RESP QUEUE===\n\n");
    for (const auto& packet : respQueue) {
        DPRINTF(MemPoolCtrl, "Response %lu\n", packet->addr);
    }

    DPRINTF(MemPoolCtrl, "\n===WRITE QUEUE===\n\n");
    for (const auto& queue : writeQueue) {
        for (const auto& packet : queue) {
            DPRINTF(MemPoolCtrl, "Write %lu\n", packet->addr);
        }
    }
#endif // TRACING_ON
}

/* supports multiple media
 * The original source code determines whether it's for DRAM or NVM by
 * compairing pkt's address field.
 * It can be generalised to compare the address with all the elements of the
 * DRAMs / NVMs in std::vector.
 * Moreover, It seems that the original code doesn't care about the situation
 * when the packet's request spans the media. (e.g., starting in DRAM range,
 * ending in NVM range) Maybe it'll be okay to follow suit... */
// New function
bool
MemPoolCtrl::recvTimingReq(PacketPtr pkt)
{
    // This is where we enter from the outside world
    DPRINTF(MemPoolCtrl, "recvTimingReq: request %s addr %lld size %d\n",
            pkt->cmdString(), pkt->getAddr(), pkt->getSize());

    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "Should only see read and writes at memory controller\n");

    // Calc avg gap between requests
    if (prevArrival != 0) {
        stats.totGap += curTick() - prevArrival;
    }
    prevArrival = curTick();

    // What type of medium does this packet access?
    /* Old code
    bool is_dram;
    if (dram && dram->getAddrRange().contains(pkt->getAddr())) {
        is_dram = true;
    } else if (nvm && nvm->getAddrRange().contains(pkt->getAddr())) {
        is_dram = false;
    } else {
        panic("Can't handle address range for packet %s\n",
              pkt->print());
    }
    */
    // New code
    DisagMemInterface* medium = nullptr;
    // DRAM
    if (dramExist()) {
        for (auto it = drams.begin(); it != drams.end(); it++) {
            if ((*it)->getAddrRange().contains(pkt->getAddr())) {
                medium = *it;
                break;
            }
        }
    }
    // NVM
    if (medium == nullptr && nvmExist()) {
        for (auto it = nvms.begin(); it != nvms.end(); it++) {
            if ((*it)->getAddrRange().contains(pkt->getAddr())) {
                medium = *it;
                break;
            }
        }
    }
    // Panic: medium not found
    if (medium == nullptr) {
        panic("[MemPoolCtrl::recvTimingReq] Can't handle address range for packet %s\n", 
                pkt->print());
    }

    // Find out how many memory packets a pkt translates to
    // If the burst size is equal or larger than the pkt size, then a pkt
    // translates to only one memory packet. Otherwise, a pkt translates to
    // multiple memory packets
    unsigned size = pkt->getSize();
    
    /* Old code
    uint32_t burst_size = is_dram ? dram->bytesPerBurst() :
                                    nvm->bytesPerBurst();
    */
    // New code
    uint32_t burst_size = medium->bytesPerBurst();

    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);

    // run the QoS scheduler and assign a QoS priority value to the packet
    qosSchedule( { &readQueue, &writeQueue }, burst_size, pkt);

    // check local buffers and do not accept if full
    if (pkt->isWrite()) {
        assert(size != 0);
        if (writeQueueFull(pkt_count)) {
            DPRINTF(MemPoolCtrl, "Write queue full, not accepting\n");
            // remember that we have to retry this port
            retryWrReq = true;
            stats.numWrRetry++;
            return false;
        } else {
            // addToWriteQueue(pkt, pkt_count, is_dram) // Old code
            addToWriteQueue(pkt, pkt_count, medium); // New code
            stats.writeReqs++;
            stats.bytesWrittenSys += size;
        }
    } else {
        assert(pkt->isRead());
        assert(size != 0);
        if (readQueueFull(pkt_count)) {
            DPRINTF(MemPoolCtrl, "Read queue full, not accepting\n");
            // remember that we have to retry this port
            retryRdReq = true;
            stats.numRdRetry++;
            return false;
        } else {
            // addToReadQueue(pkt, pkt_count, is_dram); // Old code
            addToReadQueue(pkt, pkt_count, medium); // New code
            stats.readReqs++;
            stats.bytesReadSys += size;
        }
    }

    return true;
}

// supports multiple media
// New function
void
MemPoolCtrl::processRespondEvent()
{
    DPRINTF(MemPoolCtrl,
            "processRespondEvent(): Some req has reached its readyTime\n");

    MemPoolCtrlPacket* mem_pkt = respQueue.front();

    // if (mem_pkt->isDram())  // Old code
    if (DisagDRAMInterface* pDram = mem_pkt->getDram()) {
        // media specific checks and functions when read response is complete
        pDram->respondEvent(mem_pkt->rank);
    }

    if (mem_pkt->burstHelper) {
        // it is a split packet
        mem_pkt->burstHelper->burstsServiced++;
        if (mem_pkt->burstHelper->burstsServiced ==
            mem_pkt->burstHelper->burstCount) {
            // we have now serviced all children packets of a system packet
            // so we can now respond to the requestor
            // @todo we probably want to have a different front end and back
            // end latency for split packets
            accessAndRespond(mem_pkt->pkt, frontendLatency + backendLatency);
            delete mem_pkt->burstHelper;
            mem_pkt->burstHelper = NULL;
        }
    } else {
        // it is not a split packet
        accessAndRespond(mem_pkt->pkt, frontendLatency + backendLatency);
    }

    respQueue.pop_front();

    if (!respQueue.empty()) {
        assert(respQueue.front()->readyTime >= curTick());
        assert(!respondEvent.scheduled());
        schedule(respondEvent, respQueue.front()->readyTime);
    } else {
        // if there is nothing left in any queue, signal a drain
        if (drainState() == DrainState::Draining &&
            !totalWriteQueueSize && !totalReadQueueSize &&
            allIntfDrained()) {

            DPRINTF(Drain, "Controller done draining\n");
            signalDrainDone();
        // else if (mem_pkt->isDram())  // Old code
        } else if (DisagDRAMInterface* pDram = mem_pkt->getDram()) {
            // check the refresh state and kick the refresh event loop
            // into action again if banks already closed and just waiting
            // for read to complete
            pDram->checkRefreshState(mem_pkt->rank);
        }
    }

    delete mem_pkt;

    // We have made a location in the queue available at this point,
    // so if there is a read that was forced to wait, retry now
    if (retryRdReq) {
        retryRdReq = false;
        port.sendRetryReq();
    }
}

// NOTE: Slightly modified (currently No FRFCFS policy)
// New function
MemPoolCtrlPacketQueue::iterator
MemPoolCtrl::chooseNext(MemPoolCtrlPacketQueue& queue, Tick extra_col_delay)
{
    // This method does the arbitration between requests.

    MemPoolCtrlPacketQueue::iterator ret = queue.end();

    if (!queue.empty()) {
        if (queue.size() == 1) {
            // available rank corresponds to state refresh idle
            MemPoolCtrlPacket* mem_pkt = *(queue.begin());
            if (packetReady(mem_pkt)) {
                ret = queue.begin();
                DPRINTF(MemPoolCtrl, "Single request, going to a free rank\n");
            } else {
                DPRINTF(MemPoolCtrl, "Single request, going to a busy rank\n");
            }
        } else if (memSchedPolicy == Enums::fcfs) {
            // check if there is a packet going to a free rank
            for (auto i = queue.begin(); i != queue.end(); ++i) {
                MemPoolCtrlPacket* mem_pkt = *i;
                if (packetReady(mem_pkt)) {
                    ret = i;
                    break;
                }
            }
        }
        /* No FRFCFS policy (might be changed later)
         * else if (memSchedPolicy == Enums::frfcfs) {
         *     ret = chooseNextFRFCFS(queue, extra_col_delay);
           } */
        else {
            panic("No scheduling policy chosen\n");
        }
    }
    return ret;
}

/* NOTE: It might not be applicable for disaggregated system */
/* Disable this method - might be changed later...
MemPacketQueue::iterator
MemPoolCtrl::chooseNextFRFCFS(MemPacketQueue& queue, Tick extra_col_delay)
{
    ... (skip) ...
}
*/

// supports multiple media
// New function
void
MemPoolCtrl::accessAndRespond(PacketPtr pkt, Tick static_latency)
{
    DPRINTF(MemPoolCtrl, "Responding to Address %lld.. \n",pkt->getAddr());

    bool needsResponse = pkt->needsResponse();
    // do the actual memory access which also turns the packet into a
    // response

    /* Old code
    if (dram && dram->getAddrRange().contains(pkt->getAddr())) {
        dram->access(pkt);
    } else if (nvm && nvm->getAddrRange().contains(pkt->getAddr())) {
        nvm->access(pkt);
    } else {
        panic("Can't handle address range for packet %s\n",
              pkt->print());
    }
    */
    // New code
    bool accessed = false;
    if (dramExist()) {
        for (auto it = drams.begin(); it != drams.end(); it++) {
            if ((*it)->getAddrRange().contains(pkt->getAddr())) {
                (*it)->access(pkt);
                accessed = true;
                break;
            }
        }
    }
    if (!accessed && nvmExist()) {
        for (auto it = nvms.begin(); it != nvms.end(); it++) {
            if ((*it)->getAddrRange().contains(pkt->getAddr())) {
                (*it)->access(pkt);
                accessed = true;
                break;
            }
        }
    }
    if (!accessed) {
        panic("[MemPoolCtrl::accessAndRespond] Can't handle address range for packet %s\n",
              pkt->print());
    }

    // turn packet around to go back to requestor if response expected
    if (needsResponse) {
        // access already turned the packet into a response
        assert(pkt->isResponse());
        // response_time consumes the static latency and is charged also
        // with headerDelay that takes into account the delay provided by
        // the xbar and also the payloadDelay that takes into account the
        // number of data beats.
        Tick response_time = curTick() + static_latency + pkt->headerDelay +
                             pkt->payloadDelay;
        // Here we reset the timing of the packet before sending it out.
        pkt->headerDelay = pkt->payloadDelay = 0;

        // queue the packet in the response queue to be sent out after
        // the static latency has passed
        port.schedTimingResp(pkt, response_time);
    } else {
        // @todo the packet is going to be deleted, and the MemPacket
        // is still having a pointer to it
        pendingDelete.reset(pkt);
    }

    DPRINTF(MemPoolCtrl, "Done\n");

    return;
}

void
MemPoolCtrl::pruneBurstTick()
{
    auto it = burstTicks.begin();
    while (it != burstTicks.end()) {
        auto current_it = it++;
        if (curTick() > *current_it) {
            DPRINTF(MemPoolCtrl, "Removing burstTick for %d\n", *current_it);
            burstTicks.erase(current_it);
        }
    }
}

Tick
MemPoolCtrl::getBurstWindow(Tick cmd_tick)
{
    // get tick aligned to burst window
    Tick burst_offset = cmd_tick % commandWindow;
    return (cmd_tick - burst_offset);
}

Tick
MemPoolCtrl::verifySingleCmd(Tick cmd_tick, Tick max_cmds_per_burst)
{
    // start with assumption that there is no contention on command bus
    Tick cmd_at = cmd_tick;

    // get tick aligned to burst window
    Tick burst_tick = getBurstWindow(cmd_tick);

    // verify that we have command bandwidth to issue the command
    // if not, iterate over next window(s) until slot found
    while (burstTicks.count(burst_tick) >= max_cmds_per_burst) {
        DPRINTF(MemPoolCtrl, "Contention found on command bus at %d\n",
                burst_tick);
        burst_tick += commandWindow;
        cmd_at = burst_tick;
    }

    // add command into burst window and return corresponding Tick
    burstTicks.insert(burst_tick);
    return cmd_at;
}

Tick
MemPoolCtrl::verifyMultiCmd(Tick cmd_tick, Tick max_cmds_per_burst,
                         Tick max_multi_cmd_split)
{
    // start with assumption that there is no contention on command bus
    Tick cmd_at = cmd_tick;

    // get tick aligned to burst window
    Tick burst_tick = getBurstWindow(cmd_tick);

    // Command timing requirements are from 2nd command
    // Start with assumption that 2nd command will issue at cmd_at and
    // find prior slot for 1st command to issue
    // Given a maximum latency of max_multi_cmd_split between the commands,
    // find the burst at the maximum latency prior to cmd_at
    Tick burst_offset = 0;
    Tick first_cmd_offset = cmd_tick % commandWindow;
    while (max_multi_cmd_split > (first_cmd_offset + burst_offset)) {
        burst_offset += commandWindow;
    }
    // get the earliest burst aligned address for first command
    // ensure that the time does not go negative
    Tick first_cmd_tick = burst_tick - std::min(burst_offset, burst_tick);

    // Can required commands issue?
    bool first_can_issue = false;
    bool second_can_issue = false;
    // verify that we have command bandwidth to issue the command(s)
    while (!first_can_issue || !second_can_issue) {
        bool same_burst = (burst_tick == first_cmd_tick);
        auto first_cmd_count = burstTicks.count(first_cmd_tick);
        auto second_cmd_count = same_burst ? first_cmd_count + 1 :
                                   burstTicks.count(burst_tick);

        first_can_issue = first_cmd_count < max_cmds_per_burst;
        second_can_issue = second_cmd_count < max_cmds_per_burst;

        if (!second_can_issue) {
            DPRINTF(MemPoolCtrl, "Contention (cmd2) found on command bus at %d\n",
                    burst_tick);
            burst_tick += commandWindow;
            cmd_at = burst_tick;
        }

        // Verify max_multi_cmd_split isn't violated when command 2 is shifted
        // If commands initially were issued in same burst, they are
        // now in consecutive bursts and can still issue B2B
        bool gap_violated = !same_burst &&
             ((burst_tick - first_cmd_tick) > max_multi_cmd_split);

        if (!first_can_issue || (!second_can_issue && gap_violated)) {
            DPRINTF(MemPoolCtrl, "Contention (cmd1) found on command bus at %d\n",
                    first_cmd_tick);
            first_cmd_tick += commandWindow;
        }
    }

    // Add command to burstTicks
    burstTicks.insert(burst_tick);
    burstTicks.insert(first_cmd_tick);

    return cmd_at;
}

bool
MemPoolCtrl::inReadBusState(bool next_state) const
{
    // check the bus state
    if (next_state) {
        // use busStateNext to get the state that will be used
        // for the next burst
        return (busStateNext == MemPoolCtrl::READ);
    } else {
        return (busState == MemPoolCtrl::READ);
    }
}

bool
MemPoolCtrl::inWriteBusState(bool next_state) const
{
    // check the bus state
    if (next_state) {
        // use busStateNext to get the state that will be used
        // for the next burst
        return (busStateNext == MemPoolCtrl::WRITE);
    } else {
        return (busState == MemPoolCtrl::WRITE);
    }
}

// supports multiple media
// New function
void
MemPoolCtrl::doBurstAccess(MemPoolCtrlPacket* mem_pkt)
{
    // first clean up the burstTick set, removing old entries
    // before adding new entries for next burst
    pruneBurstTick();

    // When was command issued?
    Tick cmd_at;

    // Issue the next burst and update bus state to reflect
    // when previous command was issued
    // if (mem_pkt->isDram())  // Old code
    // New code
    DisagDRAMInterface* pDram = mem_pkt->getDram();
    DisagNVMInterface* pNvm = mem_pkt->getNvm();
    if (pDram) {
        std::vector<MemPoolCtrlPacketQueue>& queue =
            selQueue(mem_pkt->isRead());

        // DisagDRAMInterface requires std::vector<MemPacketQueue>&
        std::vector<MemPacketQueue> mpq_vec;
        for (const auto& q: queue) {
            MemPacketQueue mpq;
            for (const auto mpc_pkt_ptr: q) { // Mem Pool Ctrl Packet Ptr
                mpq.push_back(static_cast<MemPacket*>(mpc_pkt_ptr));
            }
            mpq_vec.push_back(mpq);
        }

        std::tie(cmd_at, nextBurstAt) =
            pDram->doBurstAccess(mem_pkt, nextBurstAt, mpq_vec); // New code
            // dram->doBurstAccess(mem_pkt, nextBurstAt, queue); // Old code

        // Update timing for NVM ranks if NVM is configured on this channel
        /* Old code
        if (nvm)
            nvm->addRankToRankDelay(cmd_at);
        */
    } 
    // else // Old code
    // New code
    else if (pNvm) {
        std::tie(cmd_at, nextBurstAt) =
            pNvm->doBurstAccess(mem_pkt, nextBurstAt); // New code
            // nvm->doBurstAccess(mem_pkt, nextBurstAt); // Old code

        // Update timing for NVM ranks if NVM is configured on this channel
        /* Old code
        if (dram)
            dram->addRankToRankDelay(cmd_at);
        */
    }
    else {
        panic("Can't handle burst access for packet %s\n",
              mem_pkt->pkt->print());
    }
    // New code
    if (dramExist()) {
        for (auto it = drams.begin(); it != drams.end(); it++) {
            if ((*it) != pDram) {
                (*it)->addRankToRankDelay(cmd_at);
            }
        }
    }
    if (nvmExist()) {
        for (auto it = nvms.begin(); it != nvms.end(); it++) {
            if ((*it) != pNvm) {
                (*it)->addRankToRankDelay(cmd_at);
            }
        }
    }


    DPRINTF(MemPoolCtrl, "Access to %lld, ready at %lld next burst at %lld.\n",
            mem_pkt->addr, mem_pkt->readyTime, nextBurstAt);

    // Update the minimum timing between the requests, this is a
    // conservative estimate of when we have to schedule the next
    // request to not introduce any unecessary bubbles. In most cases
    // we will wake up sooner than we have to.
    
    /* Old code
    nextReqTime = nextBurstAt - (dram ? dram->commandOffset() :
                                        nvm->commandOffset());
    */
    // New code: the minimum commandOffset of every medium
    Tick min_offset = UINT64_MAX; // typedef uint64_t Tick (src/base/types.hh)
    if (dramExist()) {
        Tick dram_offset;
        for (auto it = drams.begin(); it != drams.end(); it++) {
            dram_offset = (*it)->commandOffset();
            if (dram_offset < min_offset) {
                min_offset = dram_offset;
            }
        }
    }
    if (nvmExist()) {
        Tick nvm_offset;
        for (auto it = nvms.begin(); it != nvms.end(); it++) {
            nvm_offset = (*it)->commandOffset();
            if (nvm_offset < min_offset) {
                min_offset = nvm_offset;
            }
        }
    }
    nextReqTime = nextBurstAt - min_offset;

    // Update the common bus stats
    if (mem_pkt->isRead()) {
        ++readsThisTime;
        // Update latency stats
        stats.requestorReadTotalLat[mem_pkt->requestorId()] +=
            mem_pkt->readyTime - mem_pkt->entryTime;
        stats.requestorReadBytes[mem_pkt->requestorId()] += mem_pkt->size;
    } else {
        ++writesThisTime;
        stats.requestorWriteBytes[mem_pkt->requestorId()] += mem_pkt->size;
        stats.requestorWriteTotalLat[mem_pkt->requestorId()] +=
            mem_pkt->readyTime - mem_pkt->entryTime;
    }
}

// supports multiple media
// New function
void
MemPoolCtrl::processNextReqEvent()
{
    // transition is handled by QoS algorithm if enabled
    if (turnPolicy) {
        // select bus state - only done if QoS algorithms are in use
        busStateNext = selectNextBusState();
    }

    // detect bus state change
    bool switched_cmd_type = (busState != busStateNext);
    // record stats
    recordTurnaroundStats();

    DPRINTF(MemPoolCtrl, "QoS Turnarounds selected state %s %s\n",
            (busState==MemPoolCtrl::READ)?"READ":"WRITE",
            switched_cmd_type?"[turnaround triggered]":"");

    if (switched_cmd_type) {
        if (busState == MemPoolCtrl::READ) {
            DPRINTF(MemPoolCtrl,
                    "Switching to writes after %d reads with %d reads "
                    "waiting\n", readsThisTime, totalReadQueueSize);
            stats.rdPerTurnAround.sample(readsThisTime);
            readsThisTime = 0;
        } else {
            DPRINTF(MemPoolCtrl,
                    "Switching to reads after %d writes with %d writes "
                    "waiting\n", writesThisTime, totalWriteQueueSize);
            stats.wrPerTurnAround.sample(writesThisTime);
            writesThisTime = 0;
        }
    }

    // updates current state
    busState = busStateNext;

    /* 
     * To support multiple NVM media, we need to first 'filter' the packets in
     * the queues so that each NVM medium only sees the packets they are
     * responsible for (correct address range).
     */
    /* Old code
    if (nvm) {
        for (auto queue = readQueue.rbegin();
             queue != readQueue.rend(); ++queue) {
             // select non-deterministic NVM read to issue
             // assume that we have the command bandwidth to issue this along
             // with additional RD/WR burst with needed bank operations
             if (nvm->readsWaitingToIssue()) {
                 // select non-deterministic NVM read to issue
                 nvm->chooseRead(*queue);
             }
        }
    } */
    // New code
    if (nvmExist()) {
        for (auto nvm_it = nvms.begin(); nvm_it != nvms.end(); nvm_it++) {
            auto nvm_addr_range = (*nvm_it)->getAddrRange();

            for (auto queue = readQueue.rbegin();
                    queue != readQueue.rend(); queue++) {
                // select non-deterministic NVM read to issue
                // assume that we have the command bandwidth to
                // issue this along with additional RD/WR burst
                // with needed bank operations

                if ((*nvm_it)->readsWaitingToIssue()) {
                    // filtering the packets in the queue accoring to address
                    MemPacketQueue mq;
                    for (auto pkt_it = queue->begin();
                            pkt_it != queue->end(); pkt_it++) {
                        auto packet_addr = (*pkt_it)->pkt->getAddr();
                        if (nvm_addr_range.contains(packet_addr)) {
                            // NVMinterface's chooseRead needs the parent class
                            mq.push_back(static_cast<MemPacket*>((*pkt_it)));
                        }
                    }
                    // select non-deterministic NVM read to issue
                    (*nvm_it)->chooseRead(mq);
                }
            }
        }
    }
                            
    // check ranks for refresh/wakeup - uses busStateNext, so done after
    // turnaround decisions
    // Default to busy status and update based on interface specifics
    /* TODO: change this to support multiple media
     * i.e., busy iff. all media are busy */
    /* Old code
    bool dram_busy = dram ? dram->isBusy() : true;
    bool nvm_busy = true;
    bool all_writes_nvm = false;
    if (nvm) {
        all_writes_nvm = nvm->numWritesQueued == totalWriteQueueSize;
        bool read_queue_empty = totalReadQueueSize == 0;
        nvm_busy = nvm->isBusy(read_queue_empty, all_writes_nvm);
    }
    // Default state of unused interface is 'true'
    // Simply AND the busy signals to determine if system is busy
    if (dram_busy && nvm_busy) {
        // if all ranks are refreshing wait for them to finish
        // and stall this state machine without taking any further
        // action, and do not schedule a new nextReqEvent
        return;
    }
    */
    // New code
    bool drams_busy = false;
    if (dramExist()) {
        for (auto it = drams.begin(); it != drams.end(); it++) {
            drams_busy |= (*it)->isBusy();
        }
    }

    bool nvms_busy = false;
    bool all_writes_nvm = false; // will be used later again
    if (nvmExist()) {
        uint64_t sumNumWriteQueued = 0;
        for (auto it = nvms.begin(); it != nvms.end(); it++) {
            sumNumWriteQueued += (*it)->numWritesQueued;
        }
        bool read_queue_empty = totalReadQueueSize == 0;
        bool all_writes_nvm = sumNumWriteQueued == totalWriteQueueSize;
        for (auto it = nvms.begin(); it != nvms.end(); it++) {
            nvms_busy |= (*it)->isBusy(read_queue_empty, all_writes_nvm);
        }
    }

    // if all ranks are refreshing wait for them to finish
    // and stall this state machine without taking any further
    // action, and do not schedule a new nextReqEvent
    if ((dramExist() && !nvmExist() && drams_busy) // DRAM-only case
     || (!dramExist() && nvmExist() && nvms_busy) // NVM-only case
     || (drams_busy && nvms_busy)) { // DRAM-and-NVM case
        return;
    }

    // when we get here it is either a read or a write
    if (busState == READ) {

        // track if we should switch or not
        bool switch_to_writes = false;

        if (totalReadQueueSize == 0) {
            // In the case there is no read request to go next,
            // trigger writes if we have passed the low threshold (or
            // if we are draining)
            if (!(totalWriteQueueSize == 0) &&
                (drainState() == DrainState::Draining ||
                 totalWriteQueueSize > writeLowThreshold)) {

                DPRINTF(MemPoolCtrl,
                        "Switching to writes due to read queue empty\n");
                switch_to_writes = true;
            } else {
                // check if we are drained
                // not done draining until in PWR_IDLE state
                // ensuring all banks are closed and
                // have exited low power states
                if (drainState() == DrainState::Draining &&
                    respQueue.empty() && allIntfDrained()) {

                    // Old code
                    // DPRINTF(Drain, "MemCtrl controller done draining\n");
                    // New code
                    DPRINTF(Drain, "MemPoolCtrl controller done draining\n");
                    signalDrainDone();
                }

                // nothing to do, not even any point in scheduling an
                // event for the next request
                return;
            }
        } else {
            bool read_found = false;
            MemPoolCtrlPacketQueue::iterator to_read;
            uint8_t prio = numPriorities();

            for (auto queue = readQueue.rbegin();
                 queue != readQueue.rend(); ++queue) {

                prio--;

                DPRINTF(QOS,
                        "Checking READ queue [%d] priority [%d elements]\n",
                        prio, queue->size());

                // Figure out which read request goes next
                // If we are changing command type, incorporate the minimum
                // bus turnaround delay which will be rank to rank delay
                to_read = chooseNext((*queue), switched_cmd_type ?
                                               minWriteToReadDataGap() : 0);

                if (to_read != queue->end()) {
                    // candidate read found
                    read_found = true;
                    break;
                }
            }

            // if no read to an available rank is found then return
            // at this point. There could be writes to the available ranks
            // which are above the required threshold. However, to
            // avoid adding more complexity to the code, return and wait
            // for a refresh event to kick things into action again.
            if (!read_found) {
                DPRINTF(MemPoolCtrl, "No Reads Found - exiting\n");
                return;
            }

            auto mem_pkt = *to_read;

            doBurstAccess(mem_pkt);

            // sanity check
            /* Old code
            assert(mem_pkt->size <= (mem_pkt->isDram() ?
                                      dram->bytesPerBurst() :
                                      nvm->bytesPerBurst()) );
            */
            // New code
            assert(mem_pkt->size <= (mem_pkt->getMedium()->bytesPerBurst()));
            assert(mem_pkt->readyTime >= curTick());

            // log the response
            logResponse(MemPoolCtrl::READ, (*to_read)->requestorId(),
                        mem_pkt->qosValue(), mem_pkt->getAddr(), 1,
                        mem_pkt->readyTime - mem_pkt->entryTime);


            // Insert into response queue. It will be sent back to the
            // requestor at its readyTime
            if (respQueue.empty()) {
                assert(!respondEvent.scheduled());
                schedule(respondEvent, mem_pkt->readyTime);
            } else {
                assert(respQueue.back()->readyTime <= mem_pkt->readyTime);
                assert(respondEvent.scheduled());
            }

            respQueue.push_back(mem_pkt);

            // we have so many writes that we have to transition
            // don't transition if the writeRespQueue is full and
            // there are no other writes that can issue
            /* Old code
            if ((totalWriteQueueSize > writeHighThreshold) &&
               !(nvm && all_writes_nvm && nvm->writeRespQueueFull())) {
                switch_to_writes = true;
            } */
            // New code
            if (totalWriteQueueSize > writeHighThreshold) {
                if (nvmExist()) {
                    bool allWriteRespQueueFull = true;
                    for (auto it = nvms.begin(); it != nvms.end(); it++) {
                        allWriteRespQueueFull &= (*it)->writeRespQueueFull();
                    }
                    if (!(all_writes_nvm && allWriteRespQueueFull)) {
                        switch_to_writes = true;
                    }
                }
                else {
                    switch_to_writes = true;
                }
            }
                    

            // remove the request from the queue
            // the iterator is no longer valid .
            readQueue[mem_pkt->qosValue()].erase(to_read);
        }

        // switching to writes, either because the read queue is empty
        // and the writes have passed the low threshold (or we are
        // draining), or because the writes hit the hight threshold
        if (switch_to_writes) {
            // transition to writing
            busStateNext = WRITE;
        }
    } else {
        bool write_found = false;
        MemPoolCtrlPacketQueue::iterator to_write;
        uint8_t prio = numPriorities();

        for (auto queue = writeQueue.rbegin();
             queue != writeQueue.rend(); ++queue) {

            prio--;

            DPRINTF(QOS,
                    "Checking WRITE queue [%d] priority [%d elements]\n",
                    prio, queue->size());

            // If we are changing command type, incorporate the minimum
            // bus turnaround delay
            to_write = chooseNext((*queue),
                     switched_cmd_type ? minReadToWriteDataGap() : 0);

            if (to_write != queue->end()) {
                write_found = true;
                break;
            }
        }

        // if there are no writes to a rank that is available to service
        // requests (i.e. rank is in refresh idle state) are found then
        // return. There could be reads to the available ranks. However, to
        // avoid adding more complexity to the code, return at this point and
        // wait for a refresh event to kick things into action again.
        if (!write_found) {
            DPRINTF(MemPoolCtrl, "No Writes Found - exiting\n");
            return;
        }

        auto mem_pkt = *to_write;

        // sanity check
        /* Old code
        assert(mem_pkt->size <= (mem_pkt->isDram() ?
                                  dram->bytesPerBurst() :
                                  nvm->bytesPerBurst()) ); */
        // New code
        assert(mem_pkt->size <= (mem_pkt->getMedium()->bytesPerBurst()));

        doBurstAccess(mem_pkt);

        // Old code
        // isInWriteQueue.erase(burstAlign(mem_pkt->addr, mem_pkt->isDram()));
        // New code
        isInWriteQueue.erase(burstAlign(mem_pkt->addr, mem_pkt->getMedium()));


        // log the response
        logResponse(MemPoolCtrl::WRITE, mem_pkt->requestorId(),
                    mem_pkt->qosValue(), mem_pkt->getAddr(), 1,
                    mem_pkt->readyTime - mem_pkt->entryTime);


        // remove the request from the queue - the iterator is no longer valid
        writeQueue[mem_pkt->qosValue()].erase(to_write);

        delete mem_pkt;

        // If we emptied the write queue, or got sufficiently below the
        // threshold (using the minWritesPerSwitch as the hysteresis) and
        // are not draining, or we have reads waiting and have done enough
        // writes, then switch to reads.
        // If we are interfacing to NVM and have filled the writeRespQueue,
        // with only NVM writes in Q, then switch to reads
        bool below_threshold =
            totalWriteQueueSize + minWritesPerSwitch < writeLowThreshold;

        /* Old code
        if (totalWriteQueueSize == 0 ||
            (below_threshold && drainState() != DrainState::Draining) ||
            (totalReadQueueSize && writesThisTime >= minWritesPerSwitch) ||
            (totalReadQueueSize && nvm && nvm->writeRespQueueFull() &&
             all_writes_nvm)) */
        // New code
        bool allNvmWriteRespQueueFull = true;
        if (nvmExist() && all_writes_nvm) {
            for (auto it = nvms.begin(); it != nvms.end(); it++) {
                allNvmWriteRespQueueFull &= (*it)->writeRespQueueFull();
            }
        }
        if (totalWriteQueueSize == 0 ||
            (below_threshold && drainState() != DrainState::Draining) ||
            (totalReadQueueSize && writesThisTime >= minWritesPerSwitch) ||
            (totalReadQueueSize && nvmExist() && all_writes_nvm &&
             allNvmWriteRespQueueFull)) {

            // turn the bus back around for reads again
            busStateNext = MemPoolCtrl::READ;

            // note that the we switch back to reads also in the idle
            // case, which eventually will check for any draining and
            // also pause any further scheduling if there is really
            // nothing to do
        }
    }
    // It is possible that a refresh to another rank kicks things back into
    // action before reaching this point.
    if (!nextReqEvent.scheduled())
        schedule(nextReqEvent, std::max(nextReqTime, curTick()));

    // If there is space available and we have writes waiting then let
    // them retry. This is done here to ensure that the retry does not
    // cause a nextReqEvent to be scheduled before we do so as part of
    // the next request processing
    if (retryWrReq && totalWriteQueueSize < writeBufferSize) {
        retryWrReq = false;
        port.sendRetryReq();
    }
}

// supports multiple media
// New function
bool
MemPoolCtrl::packetReady(MemPoolCtrlPacket* pkt)
{
    /* Old code
    return (pkt->isDram() ?
        dram->burstReady(pkt) : nvm->burstReady(pkt));
    */
    // New code
    return pkt->medium->burstReady(pkt);
}

// supports multiple media
// New function
Tick
MemPoolCtrl::minReadToWriteDataGap()
{
    /* Old code
    Tick dram_min = dram ?  dram->minReadToWriteDataGap() : MaxTick;
    Tick nvm_min = nvm ?  nvm->minReadToWriteDataGap() : MaxTick;
    */
    // New code
    Tick dram_min = MaxTick;
    Tick nvm_min = MaxTick;
    if (dramExist()) {
        Tick dram_gap;
        for (auto it = drams.begin(); it != drams.end(); it++) {
            dram_gap = (*it)->minReadToWriteDataGap();
            if (dram_gap < dram_min) 
                dram_min = dram_gap;
        }
    }
    if (nvmExist()) {
        Tick nvm_gap;
        for (auto it = nvms.begin(); it != nvms.end(); it++) {
            nvm_gap = (*it)->minReadToWriteDataGap();
            if (nvm_gap < nvm_min)
                nvm_min = nvm_gap;
        }
    }

    return std::min(dram_min, nvm_min);
}

// supports multiple media
// New function
Tick
MemPoolCtrl::minWriteToReadDataGap()
{
    /* Old code
    Tick dram_min = dram ? dram->minWriteToReadDataGap() : MaxTick;
    Tick nvm_min = nvm ?  nvm->minWriteToReadDataGap() : MaxTick;
    */
    // New code
    Tick dram_min = MaxTick;
    Tick nvm_min = MaxTick;
    if (dramExist()) {
        Tick dram_gap;
        for (auto it = drams.begin(); it != drams.end(); it++) {
            dram_gap = (*it)->minWriteToReadDataGap();
            if (dram_gap < dram_min) 
                dram_min = dram_gap;
        }
    }
    if (nvmExist()) {
        Tick nvm_gap;
        for (auto it = nvms.begin(); it != nvms.end(); it++) {
            nvm_gap = (*it)->minWriteToReadDataGap();
            if (nvm_gap < nvm_min)
                nvm_min = nvm_gap;
        }
    }

    return std::min(dram_min, nvm_min);
}

// changed this to support multiple media
/* Old code
Addr
MemPoolCtrl::burstAlign(Addr addr, bool is_dram) const
{
    if (is_dram)
        return (addr & ~(Addr(dram->bytesPerBurst() - 1)));
    else
        return (addr & ~(Addr(nvm->bytesPerBurst() - 1)));
} */
// New code
Addr
MemPoolCtrl::burstAlign(Addr addr, DisagMemInterface* medium)
{
    return (addr & ~(Addr(medium->bytesPerBurst() - 1)));
}

// Old signature
// MemPoolCtrl::CtrlStats::CtrlStats(MemCtrl &_ctrl)
// New signature
MemPoolCtrl::CtrlStats::CtrlStats(MemPoolCtrl &_ctrl)
    : Stats::Group(&_ctrl),
    ctrl(_ctrl),

    ADD_STAT(readReqs, "Number of read requests accepted"),
    ADD_STAT(writeReqs, "Number of write requests accepted"),

    ADD_STAT(readBursts,
             "Number of controller read bursts, "
             "including those serviced by the write queue"),
    ADD_STAT(writeBursts,
             "Number of controller write bursts, "
             "including those merged in the write queue"),
    ADD_STAT(servicedByWrQ,
             "Number of controller read bursts serviced by the write queue"),
    ADD_STAT(mergedWrBursts,
             "Number of controller write bursts merged with an existing one"),

    ADD_STAT(neitherReadNorWriteReqs,
             "Number of requests that are neither read nor write"),

    ADD_STAT(avgRdQLen, "Average read queue length when enqueuing"),
    ADD_STAT(avgWrQLen, "Average write queue length when enqueuing"),

    ADD_STAT(numRdRetry, "Number of times read queue was full causing retry"),
    ADD_STAT(numWrRetry, "Number of times write queue was full causing retry"),

    ADD_STAT(readPktSize, "Read request sizes (log2)"),
    ADD_STAT(writePktSize, "Write request sizes (log2)"),

    ADD_STAT(rdQLenPdf, "What read queue length does an incoming req see"),
    ADD_STAT(wrQLenPdf, "What write queue length does an incoming req see"),

    ADD_STAT(rdPerTurnAround,
             "Reads before turning the bus around for writes"),
    ADD_STAT(wrPerTurnAround,
             "Writes before turning the bus around for reads"),

    ADD_STAT(bytesReadWrQ, "Total number of bytes read from write queue"),
    ADD_STAT(bytesReadSys, "Total read bytes from the system interface side"),
    ADD_STAT(bytesWrittenSys,
             "Total written bytes from the system interface side"),

    ADD_STAT(avgRdBWSys, "Average system read bandwidth in MiByte/s"),
    ADD_STAT(avgWrBWSys, "Average system write bandwidth in MiByte/s"),

    ADD_STAT(totGap, "Total gap between requests"),
    ADD_STAT(avgGap, "Average gap between requests"),

    ADD_STAT(requestorReadBytes, "Per-requestor bytes read from memory"),
    ADD_STAT(requestorWriteBytes, "Per-requestor bytes write to memory"),
    ADD_STAT(requestorReadRate,
             "Per-requestor bytes read from memory rate (Bytes/sec)"),
    ADD_STAT(requestorWriteRate,
             "Per-requestor bytes write to memory rate (Bytes/sec)"),
    ADD_STAT(requestorReadAccesses,
             "Per-requestor read serviced memory accesses"),
    ADD_STAT(requestorWriteAccesses,
             "Per-requestor write serviced memory accesses"),
    ADD_STAT(requestorReadTotalLat,
             "Per-requestor read total memory access latency"),
    ADD_STAT(requestorWriteTotalLat,
             "Per-requestor write total memory access latency"),
    ADD_STAT(requestorReadAvgLat,
             "Per-requestor read average memory access latency"),
    ADD_STAT(requestorWriteAvgLat,
             "Per-requestor write average memory access latency")

{
}

void
MemPoolCtrl::CtrlStats::regStats()
{
    using namespace Stats;

    assert(ctrl.system());
    const auto max_requestors = ctrl.system()->maxRequestors();

    avgRdQLen.precision(2);
    avgWrQLen.precision(2);

    readPktSize.init(ceilLog2(ctrl.system()->cacheLineSize()) + 1);
    writePktSize.init(ceilLog2(ctrl.system()->cacheLineSize()) + 1);

    rdQLenPdf.init(ctrl.readBufferSize);
    wrQLenPdf.init(ctrl.writeBufferSize);

    rdPerTurnAround
        .init(ctrl.readBufferSize)
        .flags(nozero);
    wrPerTurnAround
        .init(ctrl.writeBufferSize)
        .flags(nozero);

    avgRdBWSys.precision(2);
    avgWrBWSys.precision(2);
    avgGap.precision(2);

    // per-requestor bytes read and written to memory
    requestorReadBytes
        .init(max_requestors)
        .flags(nozero | nonan);

    requestorWriteBytes
        .init(max_requestors)
        .flags(nozero | nonan);

    // per-requestor bytes read and written to memory rate
    requestorReadRate
        .flags(nozero | nonan)
        .precision(12);

    requestorReadAccesses
        .init(max_requestors)
        .flags(nozero);

    requestorWriteAccesses
        .init(max_requestors)
        .flags(nozero);

    requestorReadTotalLat
        .init(max_requestors)
        .flags(nozero | nonan);

    requestorReadAvgLat
        .flags(nonan)
        .precision(2);

    requestorWriteRate
        .flags(nozero | nonan)
        .precision(12);

    requestorWriteTotalLat
        .init(max_requestors)
        .flags(nozero | nonan);

    requestorWriteAvgLat
        .flags(nonan)
        .precision(2);

    for (int i = 0; i < max_requestors; i++) {
        const std::string requestor = ctrl.system()->getRequestorName(i);
        requestorReadBytes.subname(i, requestor);
        requestorReadRate.subname(i, requestor);
        requestorWriteBytes.subname(i, requestor);
        requestorWriteRate.subname(i, requestor);
        requestorReadAccesses.subname(i, requestor);
        requestorWriteAccesses.subname(i, requestor);
        requestorReadTotalLat.subname(i, requestor);
        requestorReadAvgLat.subname(i, requestor);
        requestorWriteTotalLat.subname(i, requestor);
        requestorWriteAvgLat.subname(i, requestor);
    }

    // Formula stats
    avgRdBWSys = (bytesReadSys / 1000000) / simSeconds;
    avgWrBWSys = (bytesWrittenSys / 1000000) / simSeconds;

    avgGap = totGap / (readReqs + writeReqs);

    requestorReadRate = requestorReadBytes / simSeconds;
    requestorWriteRate = requestorWriteBytes / simSeconds;
    requestorReadAvgLat = requestorReadTotalLat / requestorReadAccesses;
    requestorWriteAvgLat = requestorWriteTotalLat / requestorWriteAccesses;
}

// supports multiple media
// New function
void
MemPoolCtrl::recvFunctional(PacketPtr pkt)
{
    /* Old code
    if (dram && dram->getAddrRange().contains(pkt->getAddr())) {
        // rely on the abstract memory
        dram->functionalAccess(pkt);
    } else if (nvm && nvm->getAddrRange().contains(pkt->getAddr())) {
        // rely on the abstract memory
        nvm->functionalAccess(pkt);
    }
    */
    // New code
    bool accessed = false;
    if (dramExist()) {
        for (auto it = drams.begin(); it != drams.end(); it++) {
            if ((*it)->getAddrRange().contains(pkt->getAddr())) {
                (*it)->functionalAccess(pkt);
                accessed = true;
                break;
            }
        }
    }
    if (!accessed && nvmExist()) {
        for (auto it = nvms.begin(); it != nvms.end(); it++) {
            if ((*it)->getAddrRange().contains(pkt->getAddr())) {
                (*it)->functionalAccess(pkt);
                accessed = true;
                break;
            }
        }
    }
    if (!accessed) {
        panic("[MemPoolCtrl::recvFunctional] Can't handle address range for packet %s\n",
                pkt->print());
    }
}

Port &
MemPoolCtrl::getPort(const string &if_name, PortID idx)
{
    if (if_name != "port") {
        return QoS::MemCtrl::getPort(if_name, idx);
    } else {
        return port;
    }
}

// supports multiple media
// New function
bool
MemPoolCtrl::allIntfDrained() const
{
   /* Old code
   // ensure dram is in power down and refresh IDLE states
   bool dram_drained = !dram || dram->allRanksDrained();
   // No outstanding NVM writes
   // All other queues verified as needed with calling logic
   bool nvm_drained = !nvm || nvm->allRanksDrained();
   return (dram_drained && nvm_drained);
   */

   if (dramExist()) {
       for (auto it = drams.begin(); it != drams.end(); it++) {
           if (!(*it)->allRanksDrained())
               return false;
       }
   }

   if (nvmExist()) {
       for (auto it = nvms.begin(); it != nvms.end(); it++) {
           if (!(*it)->allRanksDrained())
               return false;
       }
   }

   return true;
}

// supports multiple media
// New function
DrainState
MemPoolCtrl::drain()
{
    // if there is anything in any of our internal queues, keep track
    // of that as well
    if (!(!totalWriteQueueSize && !totalReadQueueSize && respQueue.empty() &&
          allIntfDrained())) {

        DPRINTF(Drain, "Memory controller not drained, write: %d, read: %d,"
                " resp: %d\n", totalWriteQueueSize, totalReadQueueSize,
                respQueue.size());

        // the only queue that is not drained automatically over time
        // is the write queue, thus kick things into action if needed
        if (!totalWriteQueueSize && !nextReqEvent.scheduled()) {
            schedule(nextReqEvent, curTick());
        }

        /* Old code
        if (dram)
            dram->drainRanks();
        */
        // New code
        if (dramExist()) {
            for (auto it = drams.begin(); it != drams.end(); it++) {
                (*it)->drainRanks();
            }
        }
        return DrainState::Draining;
    } else {
        return DrainState::Drained;
    }
}

// supports multiple media
// New function
void
MemPoolCtrl::drainResume()
{
    if (!isTimingMode && system()->isTimingMode()) {
        // if we switched to timing mode, kick things into action,
        // and behave as if we restored from a checkpoint
        startup();
        // dram->startup(); // Old code
        // New code
        if (dramExist()) {
            for (auto it = drams.begin(); it != drams.end(); it++) {
                (*it)->startup();
            }
        }
    } else if (isTimingMode && !system()->isTimingMode()) {
        // if we switch from timing mode, stop the refresh events to
        // not cause issues with KVM
        /* Old code
        if (dram)
            dram->suspend();
        */
        // New code
        if (dramExist()) {
            for (auto it = drams.begin(); it != drams.end(); it++) {
                (*it)->suspend();
            }
        }
    }

    // update the mode
    isTimingMode = system()->isTimingMode();
}

MemPoolCtrl::MemoryPort::MemoryPort
(const std::string& name, MemPoolCtrl& _ctrl)
    : QueuedResponsePort(name, &_ctrl, queue), queue(_ctrl, *this, true),
      ctrl(_ctrl)
{ }

// supports multiple media
// New function
AddrRangeList
MemPoolCtrl::MemoryPort::getAddrRanges() const
{
    AddrRangeList ranges;

    /* Old code
    if (ctrl.dram) {
        DPRINTF(DRAM, "Pushing DRAM ranges to port\n");
        ranges.push_back(ctrl.dram->getAddrRange());
    }
    if (ctrl.nvm) {
        DPRINTF(NVM, "Pushing NVM ranges to port\n");
        ranges.push_back(ctrl.nvm->getAddrRange());
    }
    */
    
    // New code
    if (ctrl.dramExist()) {
        DPRINTF(DRAM, "Pushing DRAM ranges to port\n");
        for (auto it = ctrl.drams.begin(); it != ctrl.drams.end(); it++) {
            ranges.push_back((*it)->getAddrRange());
        }
    }
    if (ctrl.nvmExist()) {
        DPRINTF(NVM, "Pushing NVM ranges to port\n");
        for (auto it = ctrl.nvms.begin(); it != ctrl.nvms.end(); it++) {
            ranges.push_back((*it)->getAddrRange());
        }
    }

    return ranges;
}

void
MemPoolCtrl::MemoryPort::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(ctrl.name());

    if (!queue.trySatisfyFunctional(pkt)) {
        // Default implementation of SimpleTimingPort::recvFunctional()
        // calls recvAtomic() and throws away the latency; we can save a
        // little here by just not calculating the latency.
        ctrl.recvFunctional(pkt);
    }

    pkt->popLabel();
}

Tick
MemPoolCtrl::MemoryPort::recvAtomic(PacketPtr pkt)
{
    return ctrl.recvAtomic(pkt);
}

bool
MemPoolCtrl::MemoryPort::recvTimingReq(PacketPtr pkt)
{
    // pass it to the memory controller
    return ctrl.recvTimingReq(pkt);
}

// New function
// send the memory pool controller's subCID to memory pool manager
void
MemPoolCtrl::MemoryPort::sendMemSubCID()
{
    RequestPort* req_port = getRequestPort();
    MPMXRP* manager_port;
    if(manager_port = dynamic_cast<MPMXRP*>(req_port)) {
        manager_port->setPortMap(ctrl.memSubCID);
    }
    else {
        fatal("Memory pool controller of subCID %d is not connected to "
                "Memory pool manager\n", ctrl.memSubCID);
    }
}
        
MemPoolCtrl*
MemPoolCtrlParams::create()
{
    return new MemPoolCtrl(this);
}
