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
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
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
 * Declaration of an memory pool manager crossbar base class.
 */

#ifndef __MEM_POOL_MANAGER_XBAR_HH__
#define __MEM_POOL_MANAGER_XBAR_HH__

#include <deque>
#include <unordered_map>

#include "base/addr_range_map.hh"
#include "base/types.hh"
#include "mem/qport.hh"
#include "params/MemPoolManagerXBar.hh"
#include "sim/clocked_object.hh"
#include "sim/stats.hh"

typedef uint32_t CID;

/**
 * DisaggSim: Memory pool manager XBar
 * Basically, it's NoncoherentXBar with slightly different packet routing.
 * The MemPoolManagerXBar is responsible for the basic flow control (busy or
 * not), the administration of retries, and the address decoding.
 * NOTE: CPU side, in fact, means the interconnect side.
 * However, its name won't be changed since the packets are originating from CPUs.
 */
class MemPoolManagerXBar : public ClockedObject
{

  /* protected: */
    public: // New code: for sendMemSubCID();

    /**
     * A layer is an internal crossbar arbitration point with its own
     * flow control. Each layer is a converging multiplexer tree. By
     * instantiating one layer per destination port (and per packet
     * type, i.e. request, response, snoop request and snoop
     * response), we model full crossbar structures like AXI, ACE,
     * PCIe, etc.
     *
     * The template parameter, PortClass, indicates the destination
     * port type for the layer. The retry list holds either memory-side ports
     * or CPU-side ports, depending on the direction of the
     * layer. Thus, a request layer has a retry list containing
     * CPU-side ports, whereas a response layer holds memory-side ports.
     */
    template <typename SrcType, typename DstType>
    class Layer : public Drainable, public Stats::Group
    {

      public:

        /**
         * Create a layer and give it a name. The layer uses
         * the crossbar an event manager.
         *
         * @param _port destination port the layer converges at
         * @param _xbar the crossbar this layer belongs to
         * @param _name the layer's name
         */
        Layer(DstType& _port, MemPoolManagerXBar& _xbar,
                const std::string& _name);

        /**
         * Drain according to the normal semantics, so that the crossbar
         * can tell the layer to drain, and pass an event to signal
         * back when drained.
         *
         * @param de drain event to call once drained
         *
         * @return 1 if busy or waiting to retry, or 0 if idle
         */
        DrainState drain() override;

        const std::string name() const { return _name; }


        /**
         * Determine if the layer accepts a packet from a specific
         * port. If not, the port in question is also added to the
         * retry list. In either case the state of the layer is
         * updated accordingly.
         *
         * @param port Source port presenting the packet
         *
         * @return True if the layer accepts the packet
         */
        bool tryTiming(SrcType* src_port);

        /**
         * Deal with a destination port accepting a packet by potentially
         * removing the source port from the retry list (if retrying) and
         * occupying the layer accordingly.
         *
         * @param busy_time Time to spend as a result of a successful send
         */
        void succeededTiming(Tick busy_time);

        /**
         * Deal with a destination port not accepting a packet by
         * potentially adding the source port to the retry list (if
         * not already at the front) and occupying the layer
         * accordingly.
         *
         * @param src_port Source port
         * @param busy_time Time to spend as a result of a failed send
         */
        void failedTiming(SrcType* src_port, Tick busy_time);

        void occupyLayer(Tick until);

        /**
         * Send a retry to the port at the head of waitingForLayer. The
         * caller must ensure that the list is not empty.
         */
        void retryWaiting();

        /**
         * Handle a retry from a neighbouring module. This wraps
         * retryWaiting by verifying that there are ports waiting
         * before calling retryWaiting.
         */
        void recvRetry();

      protected:

        /**
         * Sending the actual retry, in a manner specific to the
         * individual layers. Note that for a RequestPort, there is
         * both a RequestLayer and a SnoopResponseLayer using the same
         * port, but using different functions for the flow control.
         */
        virtual void sendRetry(SrcType* retry_port) = 0;

      private:

        /** The destination port this layer converges at. */
        DstType& port;

        /** The crossbar this layer is a part of. */
        MemPoolManagerXBar& xbar;

        std::string _name;

        /**
         * We declare an enum to track the state of the layer. The
         * starting point is an idle state where the layer is waiting
         * for a packet to arrive. Upon arrival, the layer
         * transitions to the busy state, where it remains either
         * until the packet transfer is done, or the header time is
         * spent. Once the layer leaves the busy state, it can
         * either go back to idle, if no packets have arrived while it
         * was busy, or the layer goes on to retry the first port
         * in waitingForLayer. A similar transition takes place from
         * idle to retry if the layer receives a retry from one of
         * its connected ports. The retry state lasts until the port
         * in questions calls sendTiming and returns control to the
         * layer, or goes to a busy state if the port does not
         * immediately react to the retry by calling sendTiming.
         */
        enum State { IDLE, BUSY, RETRY };

        State state;

        /**
         * A deque of ports that retry should be called on because
         * the original send was delayed due to a busy layer.
         */
        std::deque<SrcType*> waitingForLayer;

        /**
         * Track who is waiting for the retry when receiving it from a
         * peer. If no port is waiting NULL is stored.
         */
        SrcType* waitingForPeer;

        /**
         * Release the layer after being occupied and return to an
         * idle state where we proceed to send a retry to any
         * potential waiting port, or drain if asked to do so.
         */
        void releaseLayer();
        EventFunctionWrapper releaseEvent;

        /**
         * Stats for occupancy and utilization. These stats capture
         * the time the layer spends in the busy state and are thus only
         * relevant when the memory system is in timing mode.
         */
        Stats::Scalar occupancy;
        Stats::Formula utilization;

    };

    class ReqLayer : public Layer<ResponsePort, RequestPort>
    {
      public:
        /**
         * Create a request layer and give it a name.
         *
         * @param _port destination port the layer converges at
         * @param _xbar the crossbar this layer belongs to
         * @param _name the layer's name
         */
        ReqLayer(RequestPort& _port, MemPoolManagerXBar& _xbar,
        const std::string& _name) :
            Layer(_port, _xbar, _name)
        {}

      protected:
        void
        sendRetry(ResponsePort* retry_port) override
        {
            retry_port->sendRetryReq();
        }
    };

    class RespLayer : public Layer<RequestPort, ResponsePort>
    {
      public:
        /**
         * Create a response layer and give it a name.
         *
         * @param _port destination port the layer converges at
         * @param _xbar the crossbar this layer belongs to
         * @param _name the layer's name
         */
        RespLayer(ResponsePort& _port, MemPoolManagerXBar& _xbar,
                  const std::string& _name) :
            Layer(_port, _xbar, _name)
        {}

      protected:
        void
        sendRetry(RequestPort* retry_port) override
        {
            retry_port->sendRetryResp();
        }
    };

    /** (from noncoherent_xbar.hh)
     * Declare the layers of this crossbar, one vector for requests
     * and one for responses.
     */
    std::vector<ReqLayer*> reqLayers;
    std::vector<RespLayer*> respLayers;

    /** (from noncoherent_xbar.hh)
     * Declaration of the non-coherent crossbar CPU-side port type, one
     * will be instantiated for each of the memory-side ports connecting to
     * the crossbar.
     */
    class MemPoolManagerXBarResponsePort : public QueuedResponsePort
    {
      private:

        /** A reference to the crossbar to which this port belongs. */
        MemPoolManagerXBar &xbar;

        /** A normal packet queue used to store responses. */
        RespPacketQueue queue;

      public:

        MemPoolManagerXBarResponsePort(const std::string &_name,
                                MemPoolManagerXBar &_xbar, PortID _id)
            : QueuedResponsePort(_name, &_xbar, queue, _id), xbar(_xbar),
              queue(_xbar, *this)
        { }

      protected:

        bool
        recvTimingReq(PacketPtr pkt) override
        {
            return xbar.recvTimingReq(pkt, id);
        }

        Tick
        recvAtomic(PacketPtr pkt) override
        {
            return xbar.recvAtomicBackdoor(pkt, id);
        }

        Tick
        recvAtomicBackdoor(PacketPtr pkt, MemBackdoorPtr &backdoor) override
        {
            return xbar.recvAtomicBackdoor(pkt, id, &backdoor);
        }

        void
        recvFunctional(PacketPtr pkt) override
        {
            xbar.recvFunctional(pkt, id);
        }

        /* Old code: cannot be used anymore, memory pool manager doesn't
         * manage address ranges */
        /*
        AddrRangeList
        getAddrRanges() const override
        {
            return xbar.getAddrRanges();
        }
        */
        /* New code: it's a vitual function and should be implemented
         * unless the parent class is modified.
         * Instead of real implementation, it just goes panic now */
        AddrRangeList
        getAddrRanges() const override
        {
            AddrRangeList _dummy;
            fatal("Memory pool manager cannot use getAddrRanges()");
            return _dummy;
        }
    };

    /** (from noncoherent_xbar.hh)
     * Declaration of the crossbar memory-side port type, one will be
     * instantiated for each of the CPU-side ports connecting to the
     * crossbar.
     */
    class MemPoolManagerXBarRequestPort : public RequestPort
    {
      private:

        /** A reference to the crossbar to which this port belongs. */
        MemPoolManagerXBar &xbar;

      public:

        MemPoolManagerXBarRequestPort(const std::string &_name,
                                 MemPoolManagerXBar &_xbar, PortID _id)
            : RequestPort(_name, &_xbar, _id), xbar(_xbar)
        { }
        
        /* New code:
         * for subCID information from memory pool controller */
        void
        setPortMap(CID memSubCID)
        {
            xbar.setPortMap(memSubCID, id);
        }

      protected:

        bool
        recvTimingResp(PacketPtr pkt) override
        {
            return xbar.recvTimingResp(pkt, id);
        }

        void
        recvRangeChange() override
        {
            xbar.recvRangeChange(id);
        }

        void
        recvReqRetry() override
        {
            xbar.recvReqRetry(id);
        }

    };

    /**
     * Cycles of front-end pipeline including the delay to accept the request
     * and to decode the address.
     */
    const Cycles frontendLatency;
    const Cycles forwardLatency;
    const Cycles responseLatency;
    /** Cycles the layer is occupied processing the packet header */
    const Cycles headerLatency;
    /** the width of the xbar in bytes */
    const uint32_t width;

    /* Old code: AddrRange -> PortID mapping */
    /* AddrRangeMap<PortID, 3> portMap; */
    /* New code: CID -> portID mapping
     * Memory pool manager has two-level routing hierarchy, i.e.,
     * CID(Component ID) in the disaggregated system and physical address */
    std::unordered_map<CID, PortID> portMap;

    /**
     * Remember where request packets came from so that we can route
     * responses to the appropriate port. This relies on the fact that
     * the underlying Request pointer inside the Packet stays
     * constant.
     */
    std::unordered_map<RequestPtr, PortID> routeTo;

    /* Old code
    all contigous ranges seen by this crossbar
    These are not necessary anymore since packets are routed by CIDs */
    /* AddrRangeList xbarRanges; */
    /* AddrRange defaultRange; */

    /* New code: implemented CID-based routing and packet processing-related
     * functions which were originally declared in noncoherent_xbar.hh */
    bool recvTimingReq(PacketPtr pkt, PortID cpu_side_port_id);
    bool recvTimingResp(PacketPtr pkt, PortID mem_side_port_id);
    void recvReqRetry(PortID mem_side_port_id);
    Tick recvAtomicBackdoor(PacketPtr pkt, PortID cpu_side_port_id,
                            MemBackdoorPtr *backdoor=nullptr);
    void recvFunctional(PacketPtr pkt, PortID cpu_side_port_id);

    /**
     * Function called by the port when the crossbar is recieving a
     * range change.
     *
     * @param mem_side_port_id id of the port that received the change
     */
    /* Changed: Now only cares whether it received all address range change
     * notifications. (no additional range management) */
    void recvRangeChange(PortID mem_side_port_id);

    /**
     * Find which port connected to this crossbar (if any) should be
     * given a packet with this address range.
     *
     * @param addr_range Address range to find port for.
     * @return id of port that the packet should be sent out of.
     */
    /* Old code */
    /* PortID findPort(AddrRange addr_range); */
    /* New code
     * Now the packet routing requires CID (and possibly other information),
     * which is recorded in each request. */
    PortID findPort(PacketPtr pkt);

    /**
     * Return the address ranges the crossbar is responsible for.
     *
     * @return a list of non-overlapping address ranges
     */
    /* Old code: memory pool manager doesn't manage address range */
    /* AddrRangeList getAddrRanges() const; */

    /**
     * Calculate the timing parameters for the packet. Updates the
     * headerDelay and payloadDelay fields of the packet
     * object with the relative number of ticks required to transmit
     * the header and the payload, respectively.
     *
     * @param pkt Packet to populate with timings
     * @param header_delay Header delay to be added
     */
    void calcPacketTiming(PacketPtr pkt, Tick header_delay);

    /**
     * Remember for each of the memory-side ports of the crossbar if we got
     * an address range from the connected CPU-side ports. For convenience,
     * also keep track of if we got ranges from all the CPU-side-port modules
     * or not.
     */
    std::vector<bool> gotAddrRanges;
    bool gotAllAddrRanges;

    /** The memory-side ports and CPU-side ports of the crossbar */
    std::vector<QueuedResponsePort*> cpuSidePorts;
    std::vector<RequestPort*> memSidePorts;

    /** Port that handles requests that don't match any of the interfaces.*/
    PortID defaultPortID;

    /** If true, use address range provided by default device.  Any
       address not handled by another port and not in default device's
       range will cause a fatal error.  If false, just send all
       addresses not handled by another port to default device. */
    /* Old code: Not necessary now since packets are routed by CIDs */
    /* const bool useDefaultRange; */

    /**
     * Stats for transaction distribution and data passing through the
     * crossbar. The transaction distribution is globally counting
     * different types of commands. The packet count and total packet
     * size are two-dimensional vectors that are indexed by the
     * CPU-side port and memory-side port id (thus the neighbouring memory-side
     * ports and neighbouring CPU-side ports), summing up both directions
     * (request and response).
     */
    Stats::Vector transDist;
    Stats::Vector2d pktCount;
    Stats::Vector2d pktSize;

    /* New code: functions used to set CID-to-Port mapping information */
    void setPortMap(CID cid, PortID portID);

  public:

    MemPoolManagerXBar(const MemPoolManagerXBarParams *p);

    virtual ~MemPoolManagerXBar();

    /** A function used to return the port associated with this object. */
    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;
    
    void regStats() override;
};

#endif //__MEM_POOL_MANAGER_XBAR_HH__
