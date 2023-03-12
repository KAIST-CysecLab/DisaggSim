#include "disaggregated_component/capability_ticket.hh"

CapabilityTicket::CapabilityTicket():
    capID(0), seqNumber(0), cpuID(0), memID(0),
    operation(MemCmd::Command::InvalidCmd), base(0x0), length(0x0)
{
}

uint64_t
CapabilityTicket::getCapID(){
    return capID;
}

void
CapabilityTicket::setCapID(uint64_t capID){
    this->capID=capID;
}

uint64_t
CapabilityTicket::getSeqNumber(){
    return seqNumber;
}

void
CapabilityTicket::setSeqNumber(uint64_t seqNumber){
    this->seqNumber=seqNumber;
}

uint64_t
CapabilityTicket::getCpuID(){
    return cpuID;
}

void
CapabilityTicket::setCpuID(uint64_t cpuID){
    this->cpuID=cpuID;
}

uint64_t
CapabilityTicket::getMemID(){
    return memID;
}

void
CapabilityTicket::setMemID(uint64_t memID){
    this->memID=memID;
}

MemCmd::Command
CapabilityTicket::getOperation(){
    return operation;
}

void
CapabilityTicket::setOperation(MemCmd::Command operation){
    this->operation=operation;
}

Addr
CapabilityTicket::getBase(){
    return base;
}

void
CapabilityTicket::setBase(Addr base){
    this->base=base;
}

Addr
CapabilityTicket::getLength(){
    return length;
}

void
CapabilityTicket::setLength(Addr length){
    this->length=length;
}


