#include "disaggregated_component/interconnect/ruby/disaggregated_ruby_system.hh"

DisaggregatedRubySystem::DisaggregatedRubySystem(const Params *p)
    : RubySystem(p)
{
}


bool DisaggregatedRubySystem::functionalRead(Packet *ptr){
    int num_controllers = m_abs_cntrl_vec.size();
    for (unsigned int i = 0; i < num_controllers; ++i) {
        if(m_abs_cntrl_vec[i]->getVersion() == (NodeID)ptr->req->getDstCID()){
            if(ptr->isRead()){
                m_abs_cntrl_vec[i]->functionalRead((Addr)NULL, ptr);
                return true;
            }else{
                fatal("DisaggregatedRubySystem::functionalRead unknown request");
            }
        }
    }
    fatal("DisaggregatedRubySystem::functionalRead Destination not found");
    return true;
}

bool DisaggregatedRubySystem::functionalWrite(Packet *ptr){
    int num_controllers = m_abs_cntrl_vec.size();
    for (unsigned int i = 0; i < num_controllers; ++i) {
        if(m_abs_cntrl_vec[i]->getVersion() == (NodeID)ptr->req->getDstCID()){
            if(ptr->isWrite()){
                m_abs_cntrl_vec[i]->functionalWrite((Addr)NULL, ptr);
                return true;
            }else{
                fatal("DisaggregatedRubySystem::functionalWrite unknown request");
            }
        }
    }
    inform("%u",ptr->getDstCID());
    fatal("DisaggregatedRubySystem::functionalWrite Destination not found");
    return true;
}

DisaggregatedRubySystem*
DisaggregatedRubySystemParams::create()
{
    return new DisaggregatedRubySystem(this);
}
