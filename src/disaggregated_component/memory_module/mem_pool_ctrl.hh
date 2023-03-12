/*
 * Copyright (c) 2012-2020 ARM Limited
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

/**
 * @file
 * MemPoolCtrl declaration
 */

#ifndef __MEM_POOL_CTRL_HH__
#define __MEM_POOL_CTRL_HH__

#include <deque>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/callback.hh"
#include "base/statistics.hh"
#include "enums/MemSched.hh"
#include "mem/qos/mem_ctrl.hh"
#include "mem/qport.hh"
// #include "params/MemCtrl.hh"
#include "params/MemPoolCtrl.hh"
#include "sim/eventq.hh"

// for MemPacket and BurstHelper class definition
#include "mem/mem_ctrl.hh"
// for DisagMemInterface and its children class definition
#include "disaggregated_component/memory_module/disag_mem_interface.hh"
// for send memory pool controller's subCID to memory pool manager
#include "disaggregated_component/memory_module/mem_pool_manager_xbar.hh"
typedef MemPoolManagerXBar::MemPoolManagerXBarRequestPort MPMXRP;

/**
 * A memory packet stores packets along with the timestamp of when
 * the packet entered the queue, and also the decoded address.
 */
/* Almost the same as MemPacket defined in mem_ctrl.hh,
 * except for some fields and methods for managing multiple media */
class MemPoolCtrlPacket: public MemPacket
{
  public:

    /* RequestorID associated with the packet */
    /* TODO: make it compatible with component id (or change it)
     * 'Request' class defines get/set Src/Dst CID methods for disaggregated
     * system components.
     * See src/mem/request.hh for more information. */
    // const RequestorID _requestorId;

    /**
     * Get the packet RequestorID
     * (interface compatibility with Packet)
     */
    // inline RequestorID requestorId() const { return _requestorId; }
    
    /* Does this packet access DRAM?*/
    /* NOTE: Don't use this field. It remains just for compatibility.
     * Use dynamic_cast<DisagDRAMInterface*> to `medium` field in the getDram(). */
    // const bool dram; // Old code, still in the parent class
    /* New code: DisagMemInterface pointer to the appropriate medium that
     * corresponds to this packet
     * Note that, the class constructor isn't modified for compatibility;
     * This field must be explicitly set by setMedium method,
     * unless this class is made from a MemPacket instance.
     */
    DisagMemInterface* medium = nullptr;

    /* NOTE: this field should be changed if there are multiple media */
    /*
     * Old: Return true if it's a DRAM access
     * For backward compatibility, This method now makes use of getDram() and
     * returns boolean value according to the result.
     */
    // inline bool isDram() const { return dram; } // Old code
    // TODO: or use fatal()?
    inline bool isDram()
    {
        if (getDram() != nullptr)
            return true;
        else
            return false;
    }
    /* New code: make use of dynamic_cast<> */
    inline DisagMemInterface* getMedium() { return medium; }
    inline void setMedium(DisagMemInterface* mem) { this->medium = mem; }
    DisagDRAMInterface* getDram();
    DisagNVMInterface* getNvm();

    // Just call the constructor of the parent class
    MemPoolCtrlPacket(PacketPtr _pkt, bool is_read, bool is_dram,
            uint8_t _rank, uint8_t _bank, uint32_t _row, uint16_t bank_id,
            Addr _addr, unsigned int _size)
        : MemPacket(_pkt, is_read, is_dram, _rank, _bank, _row, bank_id,
                _addr, _size)
    { }

    // If it should be made from the MemPacket class
    MemPoolCtrlPacket(const MemPacket* mpk, DisagMemInterface* medium)
        : MemPacket(mpk->pkt, mpk->isRead(), mpk->isDram(), mpk->rank,
                mpk->bank, mpk->row, mpk->bankId, mpk->getAddr(),
                mpk->getSize())
    { this->medium = medium; }

};

// The memory packets are store in a multiple dequeue structure,
// based on their QoS priority
typedef std::deque<MemPoolCtrlPacket*> MemPoolCtrlPacketQueue;

/**
 * The memory controller is a single-channel memory controller capturing
 * the most important timing constraints associated with a
 * contemporary controller. For multi-channel memory systems, the controller
 * is combined with a crossbar model, with the channel address
 * interleaving taking part in the crossbar.
 *
 * As a basic design principle, this controller
 * model is not cycle callable, but instead uses events to: 1) decide
 * when new decisions can be made, 2) when resources become available,
 * 3) when things are to be considered done, and 4) when to send
 * things back. The controller interfaces to media specific interfaces
 * to enable flexible topologies.
 * Through these simple principles, the model delivers
 * high performance, and lots of flexibility, allowing users to
 * evaluate the system impact of a wide range of memory technologies.
 *
 * For more details, please see Hansson et al, "Simulating DRAM
 * controllers for future system architecture exploration",
 * Proc. ISPASS, 2014. If you use this model as part of your research
 * please cite the paper.
 *
 */

/* Memory Pool Controller for disaggregated system
 * It's quite different than MemCtrl, so it's better to make a new class rather
 * than to inherit MemCtrl */

typedef uint32_t CID;

class MemPoolCtrl : public QoS::MemCtrl
{
    // for sendMemSubCID()
    friend class MemPoolManagerXBar;

  private:

    // For now, make use of a queued response port to avoid dealing with
    // flow control for the responses being sent back
    class MemoryPort : public QueuedResponsePort
    {

        RespPacketQueue queue;
        MemPoolCtrl& ctrl;

      public:

        // Old code
        // MemoryPort(const std::string& name, MemCtrl& _ctrl);
        // New code
        MemoryPort(const std::string& name, MemPoolCtrl& _ctrl);

        // New code
        void sendMemSubCID();

      protected:

        Tick recvAtomic(PacketPtr pkt);

        void recvFunctional(PacketPtr pkt);

        bool recvTimingReq(PacketPtr);

        virtual AddrRangeList getAddrRanges() const;

    };

    /* New code:
     * This memory pool controller's memory sub-CID */
    CID memSubCID;

    /**
     * Our incoming port, for a multi-ported controller add a crossbar
     * in front of it
     */
    MemoryPort port;

    /**
     * Remember if the memory system is in timing mode
     */
    bool isTimingMode;

    /**
     * Remember if we have to retry a request when available.
     */
    bool retryRdReq;
    bool retryWrReq;

    /**
     * Bunch of things requires to setup "events" in gem5
     * When event "respondEvent" occurs for example, the method
     * processRespondEvent is called; no parameters are allowed
     * in these methods
     */
    void processNextReqEvent();
    EventFunctionWrapper nextReqEvent;

    void processRespondEvent();
    EventFunctionWrapper respondEvent;

    /**
     * Check if the read queue has room for more entries
     *
     * @param pkt_count The number of entries needed in the read queue
     * @return true if read queue is full, false otherwise
     */
    /* TODO: check if this should be changed to support multiple media
     * (e.g., per-media queue, ...) 
     * NOTE: For now, multiple media just share one read queue */
    bool readQueueFull(unsigned int pkt_count) const;

    /**
     * Check if the write queue has room for more entries
     *
     * @param pkt_count The number of entries needed in the write queue
     * @return true if write queue is full, false otherwise
     */
    /* TODO: check if this should be changed to support multiple media
     * (e.g., per-media queue, ...)
     * For now, multiple media just share one write queue */
    bool writeQueueFull(unsigned int pkt_count) const;

    /**
     * When a new read comes in, first check if the write q has a
     * pending request to the same address.\ If not, decode the
     * address to populate rank/bank/row, create one or mutliple
     * "mem_pkt", and push them to the back of the read queue.
     * If this is the only read request in the system, schedule
     * an event to start servicing it.
     *
     * @param pkt The request packet from the outside world
     * @param pkt_count The number of memory bursts the pkt
     * @param is_dram Does this packet access DRAM?
     * translate to. If pkt size is larger then one full burst,
     * then pkt_count is greater than one.
     */
    /* changed this function to support multiple media */
    // Orignal signature
    // void addToReadQueue(PacketPtr pkt, unsigned int pkt_count, bool is_dram);
    // New signature
    void addToReadQueue(PacketPtr pkt, unsigned int pkt_count,
            DisagMemInterface* medium);

    /**
     * Decode the incoming pkt, create a mem_pkt and push to the
     * back of the write queue. \If the write q length is more than
     * the threshold specified by the user, ie the queue is beginning
     * to get full, stop reads, and start draining writes.
     *
     * @param pkt The request packet from the outside world
     * @param pkt_count The number of memory bursts the pkt
     * @param is_dram Does this packet access DRAM?
     * translate to. If pkt size is larger then one full burst,
     * then pkt_count is greater than one.
     */
    /* changed this function to support multiple media */
    // Original signature
    // void addToWriteQueue(PacketPtr pkt, unsigned int pkt_count,
    //     bool is_dram);
    // New signature
    void addToWriteQueue(PacketPtr pkt, unsigned int pkt_count,
            DisagMemInterface* medium);

    /**
     * Actually do the burst based on media specific access function.
     * Update bus statistics when complete.
     *
     * @param mem_pkt The memory packet created from the outside world pkt
     */
    /* changed this function to support multiple media */
    void doBurstAccess(MemPoolCtrlPacket* mem_pkt);

    /**
     * When a packet reaches its "readyTime" in the response Q,
     * use the "access()" method in AbstractMemory to actually
     * create the response packet, and send it back to the outside
     * world requestor.
     *
     * @param pkt The packet from the outside world
     * @param static_latency Static latency to add before sending the packet
     */
    /* changed this function to support multiple media */
    void accessAndRespond(PacketPtr pkt, Tick static_latency);

    /**
     * Determine if there is a packet that can issue.
     *
     * @param pkt The packet to evaluate
     */
    /* changed this function to support multiple media */
    bool packetReady(MemPoolCtrlPacket* pkt);

    /**
     * Calculate the minimum delay used when scheduling a read-to-write
     * transision.
     * @param return minimum delay
     */
    /* changed this function to support multiple media */
    Tick minReadToWriteDataGap();

    /**
     * Calculate the minimum delay used when scheduling a write-to-read
     * transision.
     * @param return minimum delay
     */
    /* changed this function to support multiple media */
    Tick minWriteToReadDataGap();

    /**
     * The memory schduler/arbiter - picks which request needs to
     * go next, based on the specified policy such as FCFS or FR-FCFS
     * and moves it to the head of the queue.
     * Prioritizes accesses to the same rank as previous burst unless
     * controller is switching command type.
     *
     * @param queue Queued requests to consider
     * @param extra_col_delay Any extra delay due to a read/write switch
     * @return an iterator to the selected packet, else queue.end()
     */
    // changed this function: currently only FCFS for disaggregated system
    MemPoolCtrlPacketQueue::iterator chooseNext(MemPoolCtrlPacketQueue& queue,
        Tick extra_col_delay);

    /**
     * For FR-FCFS policy reorder the read/write queue depending on row buffer
     * hits and earliest bursts available in memory
     *
     * @param queue Queued requests to consider
     * @param extra_col_delay Any extra delay due to a read/write switch
     * @return an iterator to the selected packet, else queue.end()
     */
    /* NOTE: It might not be applicable for disaggregated system */
    /* commented this out - might be changed later...
    MemPoolCtrlPacketQueue::iterator
    chooseNextFRFCFS(MemPoolCtrlPacketQueue& queue, Tick extra_col_delay); */

    /**
     * Calculate burst window aligned tick
     *
     * @param cmd_tick Initial tick of command
     * @return burst window aligned tick
     */
    Tick getBurstWindow(Tick cmd_tick);

    /**
     * Used for debugging to observe the contents of the queues.
     */
    void printQs() const;

    /**
     * Burst-align an address.
     *
     * @param addr The potentially unaligned address
     * @param is_dram Does this packet access DRAM?
     *
     * @return An address aligned to a memory burst
     */
    /* changed this function to support multiple media */
    // Original signature
    // Addr burstAlign(Addr addr, bool is_dram) const;
    // New signature
    Addr burstAlign(Addr addr, DisagMemInterface* medium);

    /**
     * The controller's main read and write queues,
     * with support for QoS reordering
     */
    /* TODO: check if the queues should be changed to support multiple media
     * NOTE: For now, multiple media just share one read / write queue */
    std::vector<MemPoolCtrlPacketQueue> readQueue;
    std::vector<MemPoolCtrlPacketQueue> writeQueue;

    /**
     * To avoid iterating over the write queue to check for
     * overlapping transactions, maintain a set of burst addresses
     * that are currently queued. Since we merge writes to the same
     * location we never have more than one address to the same burst
     * address.
     */
    /* TODO: check if the queues should be changed to support multiple media
     * NOTE: For now, multiple media just share one write queue */
    std::unordered_set<Addr> isInWriteQueue;

    /**
     * Response queue where read packets wait after we're done working
     * with them, but it's not time to send the response yet. The
     * responses are stored separately mostly to keep the code clean
     * and help with events scheduling. For all logical purposes such
     * as sizing the read queue, this and the main read queue need to
     * be added together.
     */
    /* TODO: check if the queues should be changed to support multiple media
     * NOTE: For now, multiple media just share one reponse queue */
    std::deque<MemPoolCtrlPacket*> respQueue;

    /**
     * Holds count of commands issued in burst window starting at
     * defined Tick. This is used to ensure that the command bandwidth
     * does not exceed the allowable media constraints.
     */
    std::unordered_multiset<Tick> burstTicks;

    /**
     * Create pointer to interface of the actual dram media when connected
     */
    /* TODO: change any reference to this field to support multiple DRAMs */
    // DisagDRAMInterface* const dram; // Original DRAM field
    std::vector<DisagDRAMInterface*> drams; // New DRAMs vector

    /**
     * Create pointer to interface of the actual nvm media when connected
     */
    /* TODO: change any reference to this field to support multiple NVMs */
    // DisagNVMInterface* const nvm; // Original NVM field
    std::vector<DisagNVMInterface*> nvms; // New NVMs vector

    /**
     * The following are basic design parameters of the memory
     * controller, and are initialized based on parameter values.
     * The rowsPerBank is determined based on the capacity, number of
     * ranks and banks, the burst size, and the row buffer size.
     */
    // NOTE: Removed const, it will be initialized by the constructor
    /* Old code
    const uint32_t readBufferSize;
    const uint32_t writeBufferSize;
    const uint32_t writeHighThreshold;
    const uint32_t writeLowThreshold; */
    // New code
    uint32_t readBufferSize;
    uint32_t writeBufferSize;
    uint32_t writeHighThreshold;
    uint32_t writeLowThreshold;

    const uint32_t minWritesPerSwitch;
    uint32_t writesThisTime;
    uint32_t readsThisTime;

    /**
     * Memory controller configuration initialized based on parameter
     * values.
     */
    /* NOTE: currently only FCFS is used for disaggregated system simulation
     * (might be changed later) */
    Enums::MemSched memSchedPolicy;

    /** TODO: Latencies and delays for disaggregated system should be
     * adjusted after its implementation */

    /**
     * Pipeline latency of the controller frontend. The frontend
     * contribution is added to writes (that complete when they are in
     * the write buffer) and reads that are serviced the write buffer.
     */
    const Tick frontendLatency;

    /**
     * Pipeline latency of the backend and PHY. Along with the
     * frontend contribution, this latency is added to reads serviced
     * by the memory.
     */
    const Tick backendLatency;

    /**
     * Length of a command window, used to check
     * command bandwidth
     */
    const Tick commandWindow;

    /**
     * Till when must we wait before issuing next RD/WR burst?
     */
    Tick nextBurstAt;

    Tick prevArrival;

    /**
     * The soonest you have to start thinking about the next request
     * is the longest access time that can occur before
     * nextBurstAt. Assuming you need to precharge, open a new row,
     * and access, it is tRP + tRCD + tCL.
     */
    Tick nextReqTime;

    struct CtrlStats : public Stats::Group
    {
        // CtrlStats(MemCtrl &ctrl); // Old code
        CtrlStats(MemPoolCtrl &ctrl); // New code

        void regStats() override;

        // MemCtrl &ctrl; // Old code
        MemPoolCtrl &ctrl; // New code

        // All statistics that the model needs to capture
        Stats::Scalar readReqs;
        Stats::Scalar writeReqs;
        Stats::Scalar readBursts;
        Stats::Scalar writeBursts;
        Stats::Scalar servicedByWrQ;
        Stats::Scalar mergedWrBursts;
        Stats::Scalar neitherReadNorWriteReqs;
        // Average queue lengths
        Stats::Average avgRdQLen;
        Stats::Average avgWrQLen;

        Stats::Scalar numRdRetry;
        Stats::Scalar numWrRetry;
        Stats::Vector readPktSize;
        Stats::Vector writePktSize;
        Stats::Vector rdQLenPdf;
        Stats::Vector wrQLenPdf;
        Stats::Histogram rdPerTurnAround;
        Stats::Histogram wrPerTurnAround;

        Stats::Scalar bytesReadWrQ;
        Stats::Scalar bytesReadSys;
        Stats::Scalar bytesWrittenSys;
        // Average bandwidth
        Stats::Formula avgRdBWSys;
        Stats::Formula avgWrBWSys;

        Stats::Scalar totGap;
        Stats::Formula avgGap;

        // per-requestor bytes read and written to memory
        Stats::Vector requestorReadBytes;
        Stats::Vector requestorWriteBytes;

        // per-requestor bytes read and written to memory rate
        Stats::Formula requestorReadRate;
        Stats::Formula requestorWriteRate;

        // per-requestor read and write serviced memory accesses
        Stats::Vector requestorReadAccesses;
        Stats::Vector requestorWriteAccesses;

        // per-requestor read and write total memory access latency
        Stats::Vector requestorReadTotalLat;
        Stats::Vector requestorWriteTotalLat;

        // per-requestor raed and write average memory access latency
        Stats::Formula requestorReadAvgLat;
        Stats::Formula requestorWriteAvgLat;
    };

    CtrlStats stats;

    /**
     * Upstream caches need this packet until true is returned, so
     * hold it for deletion until a subsequent call
     */
    /* TODO: check if this field should be changed to support multiple CPU-RAM
     * connections 
     * NOTE: the original gem5 source also has 'todo' comment in mem_ctrl.cc */
    std::unique_ptr<Packet> pendingDelete;

    /**
     * Select either the read or write queue
     *
     * @param is_read The current burst is a read, select read queue
     * @return a reference to the appropriate queue
     */
    /* TODO: check if this should be changed to support multiple media
     * NOTE: currently multiple media just share one read / write queue */
    std::vector<MemPoolCtrlPacketQueue>& selQueue(bool is_read)
    {
        return (is_read ? readQueue : writeQueue);
    };

    /**
     * Remove commands that have already issued from burstTicks
     */
    void pruneBurstTick();

  public:

    MemPoolCtrl(const MemPoolCtrlParams* p);
    
    /* New code:
     * memory sub CID getter (setting is done by constructor) */
    CID getMemSubCID() { return memSubCID; }

    /**
     * Ensure that all interfaced have drained commands
     *
     * @return bool flag, set once drain complete
     */
    bool allIntfDrained() const;

    DrainState drain() override;

    /**
     * Check for command bus contention for single cycle command.
     * If there is contention, shift command to next burst.
     * Check verifies that the commands issued per burst is less
     * than a defined max number, maxCommandsPerWindow.
     * Therefore, contention per cycle is not verified and instead
     * is done based on a burst window.
     *
     * @param cmd_tick Initial tick of command, to be verified
     * @param max_cmds_per_burst Number of commands that can issue
     *                           in a burst window
     * @return tick for command issue without contention
     */
    Tick verifySingleCmd(Tick cmd_tick, Tick max_cmds_per_burst);

    /**
     * Check for command bus contention for multi-cycle (2 currently)
     * command. If there is contention, shift command(s) to next burst.
     * Check verifies that the commands issued per burst is less
     * than a defined max number, maxCommandsPerWindow.
     * Therefore, contention per cycle is not verified and instead
     * is done based on a burst window.
     *
     * @param cmd_tick Initial tick of command, to be verified
     * @param max_multi_cmd_split Maximum delay between commands
     * @param max_cmds_per_burst Number of commands that can issue
     *                           in a burst window
     * @return tick for command issue without contention
     */
    Tick verifyMultiCmd(Tick cmd_tick, Tick max_cmds_per_burst,
                        Tick max_multi_cmd_split = 0);

    /**
     * Is there a respondEvent scheduled?
     *
     * @return true if event is scheduled
     */
    bool respondEventScheduled() const { return respondEvent.scheduled(); }

    /**
     * Is there a read/write burst Event scheduled?
     *
     * @return true if event is scheduled
     */
    bool requestEventScheduled() const { return nextReqEvent.scheduled(); }

    /**
     * restart the controller
     * This can be used by interfaces to restart the
     * scheduler after maintainence commands complete
     *
     * @param Tick to schedule next event
     */
    void restartScheduler(Tick tick) { schedule(nextReqEvent, tick); }

    /**
     * Check the current direction of the memory channel
     *
     * @param next_state Check either the current or next bus state
     * @return True when bus is currently in a read state
     */
    bool inReadBusState(bool next_state) const;

    /**
     * Check the current direction of the memory channel
     *
     * @param next_state Check either the current or next bus state
     * @return True when bus is currently in a write state
     */
    bool inWriteBusState(bool next_state) const;

    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;

    virtual void init() override;
    virtual void startup() override;
    virtual void drainResume() override;

    // New code
    inline bool dramExist() const {
        return (drams.empty() ? false : true);
    }
    inline bool nvmExist() const {
        return (nvms.empty() ? false : true);
    }

  protected:

    Tick recvAtomic(PacketPtr pkt);
    void recvFunctional(PacketPtr pkt);
    bool recvTimingReq(PacketPtr pkt);

};

#endif //__MEM_POOL_CTRL_HH__
