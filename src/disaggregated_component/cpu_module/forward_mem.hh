#ifndef __DISAGREGATED_COMPONENT_CPU_MODULE_FORWARD_MEM_HH__
#define __DISAGREGATED_COMPONENT_CPU_MODULE_FORWARD_MEM_HH__

#include <queue>

#include "mem/abstract_mem.hh"
#include "params/ForwardMemory.hh"
#include "sim/system.hh"

class ForwardMemory : public AbstractMemory
{
     private:

        class ForwardResponsePort : public ResponsePort
        {

        private:

            friend class ForwardRequestPort;

            ForwardMemory* owner;

            // true if this port contitutes external interface
            // (i.e. towards disaggregated interconnect)
            bool isExternal;

        public:

            ForwardResponsePort(const std::string& name,
                ForwardMemory* owner, bool isExternal);

        protected:

            Tick recvAtomic(PacketPtr pkt)
                { panic("recvAtomic unimpl."); }

            void recvFunctional(PacketPtr pkt);

            bool recvTimingReq(PacketPtr pkt);

            void recvRespRetry();

            AddrRangeList getAddrRanges() const;

        };

        class ForwardRequestPort : public RequestPort
        {

        private:

            friend class ForwardResponsePort;

            ForwardMemory* owner;

            // true if this port contitutes external interface
            // (i.e. towards disaggregated interconnect)
            bool isExternal;

        public:

            ForwardRequestPort(const std::string& _name,
                ForwardMemory* _owner, bool _isExternal);

        protected:

            bool recvTimingResp(PacketPtr pkt) override;

            void recvReqRetry() override;

        };


        ForwardResponsePort externalResponder;
        ForwardRequestPort externalRequestor;


        ForwardResponsePort internalResponder;
        ForwardRequestPort internalRequestor;

    public:
        typedef ForwardMemoryParams Params;
        ForwardMemory(Params *p);

        Port& getPort(const std::string &if_name,
            PortID idx=InvalidPortID) override;

        void init() override;
};


#endif
