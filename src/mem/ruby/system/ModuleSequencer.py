
from m5.params import *
from m5.proxy import *
from m5.objects.ClockedObject import ClockedObject

class ModuleSequencer(ClockedObject):
   type = 'ModuleSequencer'
   cxx_header = "mem/ruby/system/ModuleSequencer.hh"
   responder = ResponsePort("sequencer response port")
   requestor = RequestPort("sequencer request port")
   ruby_system = Param.RubySystem("")