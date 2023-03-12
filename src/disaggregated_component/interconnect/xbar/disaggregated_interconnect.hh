#ifndef __DISAGREGATED_COMPONENT_INTERCONNECT_DISAGGREGATED_INTERCONNECT_HH__
#define __DISAGREGATED_COMPONENT_INTERCONNECT_DISAGGREGATED_INTERCONNECT_HH__

#include "mem/xbar.hh"
#include "params/DisaggregatedInterconnect.hh"

/*
    Refer to NoncoherentXbar for more detail
*/

class DisaggregatedInterconnect : public BaseXBar{


  private:
    std::vector<PortID> cidToPortIDMap;

  protected:

    /**
     * Declare the layers of this crossbar, one vector for requests
     * and one for responses.
     */
    std::vector<ReqLayer*> reqLayers;
    std::vector<RespLayer*> respLayers;

    /**
     * Declaration of the non-coherent crossbar response port type, one
     * will be instantiated for each of the request ports connecting to
     * the crossbar.
     */
    class DisaggregatedInterconnectResponsePort : public QueuedResponsePort
    {
      private:

        /** A reference to the crossbar to which this port belongs. */
        DisaggregatedInterconnect &interconnect;

        /** A normal packet queue used to store responses. */
        RespPacketQueue queue;

      public:

        DisaggregatedInterconnectResponsePort(
          const std::string &_name,
          DisaggregatedInterconnect &_interconnect, PortID _id
        ):
          QueuedResponsePort(_name, &_interconnect, queue, _id),
          interconnect(_interconnect),
          queue(_interconnect, *this)
        { }

      protected:

        bool
        recvTimingReq(PacketPtr pkt) override
        {
            return interconnect.recvTimingReq(pkt, id);
        }

        Tick
        recvAtomic(PacketPtr pkt) override
        {
            return interconnect.recvAtomicBackdoor(pkt, id);
        }

        Tick
        recvAtomicBackdoor(PacketPtr pkt, MemBackdoorPtr &backdoor) override
        {
            return interconnect.recvAtomicBackdoor(pkt, id, &backdoor);
        }

        void
        recvFunctional(PacketPtr pkt) override
        {
            interconnect.recvFunctional(pkt, id);
        }

        AddrRangeList
        getAddrRanges() const override
        {
            return interconnect.getAddrRanges();
        }
    };

    /**
     * Declaration of the crossbar request port type, one will be
     * instantiated for each of the response ports connecting to the
     * crossbar.
     */
    class DisaggregatedInterconnectRequestPort : public RequestPort
    {
      private:

        /** A reference to the crossbar to which this port belongs. */
        DisaggregatedInterconnect &interconnect;

      public:

        DisaggregatedInterconnectRequestPort(
          const std::string &_name,
          DisaggregatedInterconnect &_interconnect,
          PortID _id
        ):
          RequestPort(_name, &_interconnect, _id),
          interconnect(_interconnect)
        { }

      protected:

        bool
        recvTimingResp(PacketPtr pkt) override
        {
            return interconnect.recvTimingResp(pkt, id);
        }

        void
        recvRangeChange() override
        {
            interconnect.recvRangeChange(id);
        }

        void
        recvReqRetry() override
        {
            interconnect.recvReqRetry(id);
        }
    };

    PortID findPort(uint32_t dst_cid);

    virtual bool recvTimingReq(PacketPtr pkt, PortID response_port_id);
    virtual bool recvTimingResp(PacketPtr pkt, PortID request_port_id);
    void recvReqRetry(PortID request_port_id);
    Tick recvAtomicBackdoor(PacketPtr pkt, PortID response_port_id,
                            MemBackdoorPtr *backdoor=nullptr);
    void recvFunctional(PacketPtr pkt, PortID response_port_id);

    void recvRangeChange(PortID request_port_id) override;

  public:
    typedef DisaggregatedInterconnectParams Params;
    DisaggregatedInterconnect(Params* p);

    virtual ~DisaggregatedInterconnect();

};

#endif
