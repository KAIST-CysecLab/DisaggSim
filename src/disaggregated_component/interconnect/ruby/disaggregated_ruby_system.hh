
#ifndef __DISAGGREGATED_COMPONENT_INTERCONNCET_RUBY_DISAGGREGATED_RUBY_SYSTEM_HH__
#define __DISAGGREGATED_COMPONENT_INTERCONNCET_RUBY_DISAGGREGATED_RUBY_SYSTEM_HH__

#include "mem/ruby/system/RubySystem.hh"
#include "params/DisaggregatedRubySystem.hh"

class DisaggregatedRubySystem : public RubySystem
{
  public:
    typedef DisaggregatedRubySystemParams Params;
    DisaggregatedRubySystem(const Params *p);

    bool functionalRead(Packet *ptr);
    bool functionalWrite(Packet *ptr);
};

#endif
