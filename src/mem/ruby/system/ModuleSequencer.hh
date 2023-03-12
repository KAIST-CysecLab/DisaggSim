
#ifndef __DISAGGREGATED_MEM_RUBY_SYSTEM_MODULE_SEQUENCER_HH__
#define __DISAGGREGATED_MEM_RUBY_SYSTEM_MODULE_SEQUENCER_HH__

#include <memory>
#include <ostream>
#include <unordered_map>

#include "mem/ruby/common/Address.hh"
#include "mem/ruby/protocol/RubyRequest.hh"
#include "mem/ruby/system/RubySystem.hh"
#include "mem/ruby/common/MachineID.hh"
#include "mem/ruby/network/MessageBuffer.hh"
#include "params/ModuleSequencer.hh"
#include "mem/ruby/protocol/MemoryMsg.hh"
#include "sim/clocked_object.hh"

class ModuleSequencer : public ClockedObject
{

  public:
    class SequencerRequestPort : public RequestPort
    {
      public:
        SequencerRequestPort(const std::string& name, ModuleSequencer *owner);

      protected:
        bool recvTimingResp(PacketPtr pkt);
        void recvRangeChange() {}
        void recvReqRetry(){
        // TODO: request retry?
        }
      private:
        ModuleSequencer* owner;
    };

    class SequencerResponsePort : public ResponsePort
    {
      public:
        SequencerResponsePort(const std::string& name, ModuleSequencer *owner);

      protected:
        bool recvTimingReq(PacketPtr pkt);

        Tick recvAtomic(PacketPtr pkt){
          panic("Atomic not supported for module sequencer\n");
        }

        void recvFunctional(PacketPtr pkt);

        void recvRespRetry(){
        // TODO: request retry?
        }

        AddrRangeList getAddrRanges() const
        { AddrRangeList ranges; return ranges; }
      private:
        ModuleSequencer* owner;

    };
  
    typedef ModuleSequencerParams Params;
    ModuleSequencer(const Params *);
    void init() override;
    void makeRequest();
    void makeResponse(PacketPtr pkt);

    Port &getPort(const std::string &if_name, PortID idx=InvalidPortID) override;

    /* SLICC Helper */
    MachineID getDstMachineID(PacketPtr pkt, MachineType mtype);
    bool functionalRequest(PacketPtr pkt);
    Addr dummyAddr(){
      return 0;
    }

    /* SLICC callback */
    // read response received (ack)
    void dataCallback(const PacketPtr& pkt);
    // write completion (ack)
    void writeCompleteCallback(const PacketPtr& pkt);
    // request received
    void requestCallback(const PacketPtr& pkt);

    RubySystem* rs;
    bool isCPUSequencer() { 
      fatal("isCPUSequencer should not be used"); return false; 
    }
    void setController(AbstractController* _cntrl) { controller = _cntrl; }
  protected:
    struct SenderState : public Packet::SenderState
    {
        // Id of the machine from which the request originated.
        MachineID id;

        SenderState(MachineID _id) : id(_id)
        {}
    };

  private:
    SequencerRequestPort requestor;
    SequencerResponsePort responder;
    AbstractController* controller;
    MessageBuffer* mandatory_q_ptr;
    MessageBuffer* responseFromMemory_ptr;
};

#endif // __DISAGGREGATED_COMPONENT_INTERCONNCET_RUBY_MODULE_SEQUENCER_HH__
