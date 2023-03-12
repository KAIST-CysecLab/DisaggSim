#ifndef __DISAGGREGATED_COMPONENT_CAPABILITY_TICKET_HH__
#define __DISAGGREGATED_COMPONENT_CAPABILITY_TICEKT_HH__

#include "mem/packet.hh"

class CapabilityTicket{

    private:
        uint64_t capID;
        uint64_t seqNumber;
        uint64_t cpuID;
        uint64_t memID;
        MemCmd::Command operation;
        Addr base;
        Addr length;


    public:

        CapabilityTicket();

        uint64_t getCapID();
        void setCapID(uint64_t capID);

        uint64_t getSeqNumber();
        void setSeqNumber(uint64_t seqNumber);

        uint64_t getCpuID();
        void setCpuID(uint64_t cpuID);

        uint64_t getMemID();
        void setMemID(uint64_t memID);

        MemCmd::Command getOperation();
        void setOperation(MemCmd::Command operation);

        Addr getBase();
        void setBase(Addr base);

        Addr getLength();
        void setLength(Addr length);



};
#endif
