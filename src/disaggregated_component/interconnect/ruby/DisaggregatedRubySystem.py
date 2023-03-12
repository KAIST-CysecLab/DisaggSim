
from m5.params import *
from m5.proxy import *
from m5.objects.RubySystem import RubySystem

class DisaggregatedRubySystem(RubySystem):
   type = 'DisaggregatedRubySystem'
   cxx_header = "disaggregated_component/interconnect/ruby/disaggregated_ruby_system.hh"