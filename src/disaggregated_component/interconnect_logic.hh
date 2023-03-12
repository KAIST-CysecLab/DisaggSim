#ifndef __DISAGREGATED_COMPONENT_INTERCONNECT_LOGIC_HH__
#define __DISAGREGATED_COMPONENT_INTERCONNECT_LOGIC_HH__

#include <deque>

#include "base/statistics.hh"
#include "disaggregated_component/base/base_module.hh"
#include "disaggregated_component/capability_ticket.hh"
#include "mem/port.hh"
#include "params/InterconnectLogic.hh"
#include "sim/clocked_object.hh"

class InterconnectLogic: public ClockedObject
{


    public:

        /**
         * Port for receiving requests (i.e. Port for sending responses)
         */
        class DisaggregatedResponsePort : public ResponsePort
        {
            private:
                friend class InterconnectLogic;

                bool needRetry;

                /// The object that owns this object
                InterconnectLogic *owner;

                // true if this port contitutes external interface
                // (i.e. towards disaggregated interconnect)
                bool isExternal;

                bool trySendRetry();

            public:
                DisaggregatedResponsePort(const std::string& name,
                    InterconnectLogic *owner, bool isExternal);

                AddrRangeList getAddrRanges() const override;

                uint32_t getCID() const
                { return owner->getCID();}


            protected:

                Tick recvAtomic(PacketPtr pkt) override
                { panic("recvAtomic unimpl."); }

                void recvFunctional(PacketPtr pkt) override;

                bool recvTimingReq(PacketPtr pkt) override;

                void recvRespRetry() override;
        };

        /**
         * Port for sending requests (i.e. Port for receiving responses)
         */
        class DisaggregatedRequestPort : public RequestPort
        {
            private:

                friend class InterconnectLogic;

                bool needRetry;

                /// The object that owns this object (SimpleCache)
                InterconnectLogic *owner;

                // true if this port contitutes external interface
                // (i.e. towards disaggregated interconnect)
                bool isExternal;

                bool trySendRetry();

            public:
                DisaggregatedRequestPort(const std::string& name,
                    InterconnectLogic *owner, bool isExternal);

                uint32_t getCID() const
                { return owner->getCID();}

            protected:

                bool recvTimingResp(PacketPtr pkt) override;

                void recvReqRetry() override;

        };

    private:

        class DeferredPacket
        {

        public:

            Tick availTick;
            PacketPtr pkt;
            Tick delay;

            DeferredPacket(PacketPtr _pkt, Tick _tick, Tick _delay):
                availTick(_tick), pkt(_pkt), delay(_delay)
            { }

            bool operator< (const DeferredPacket & rhs) {
                return this->availTick < rhs.availTick;
            }

        };

        unsigned numSndProcessingUnit;
        unsigned numRcvProcessingUnit;

        Tick* sndProcessingUnitAvailableTimes;
        Tick* rcvProcessingUnitAvailableTimes;

        // For representing the interface to the disaggregated interconnect
        DisaggregatedResponsePort externalResponder;
        DisaggregatedRequestPort externalRequestor;

        //For representing the connection between cpu and this logic
        DisaggregatedResponsePort internalResponder;
        DisaggregatedRequestPort internalRequestor;

        bool sndWait = false;
        bool rcvWait = false;

        // Queue Limits
        uint32_t rcvLimit;
        uint32_t sndLimit;

        // Processing queues contain the packets that are not yet processed (i.e. translation)
        // Ready queues contain the packets that are processed but not yet transmitted
        std::deque<DeferredPacket> rcvProcessingQueue;
        std::deque<DeferredPacket> rcvReadyQueue;
        std::deque<DeferredPacket> sndProcessingQueue;
        std::deque<DeferredPacket> sndReadyQueue;

        // Latency for logic operation (translation, etc)
        const Tick latency;

        BaseModule* module;

        AddrRange range;

        //component id
        uint32_t cid;

        bool isRequireReceiveTranslation;
        bool isRequireSendTranslation;

        TranslationInformation* translationInformation;

        // for statistics
        Stats::Scalar numTX;
        Stats::Scalar numRX;

        CapabilityTicket ticket;

        void checkTicket(PacketPtr pkt);

        // Receive (i.e. forward to CPU) with first-come-first-served policy
        void processReceive();
        void processReceive(PacketPtr pkt);
        void completeReceive();

        // Send (i.e. forward to Disaggregated-Interconnect) processed packets
        void processSend();
        void processSend(PacketPtr pkt);
        void completeSend();

        bool isRcvFull(){
            return false; 
            //return rcvProcessingQueue.size() + rcvReadyQueue.size() == rcvLimit;
            }
        bool isSndFull(){
            return false; 
            //return sndProcessingQueue.size() + sndReadyQueue.size() == sndLimit;
        }

        Tick acquireAvailableSndProcessingUnit(Tick useTime);
        Tick acquireAvailableRcvProcessingUnit(Tick useTime);

    public:
        typedef InterconnectLogicParams Params;
        InterconnectLogic(Params* p);
        ~InterconnectLogic();

        Port &getPort(const std::string &if_name,
                    PortID idx=InvalidPortID) override;

        void regStats() override;

        void init() override;

        uint32_t getCID()
        {return cid;}

};

#endif
