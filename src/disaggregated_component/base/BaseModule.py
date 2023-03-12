from m5.params import *
from m5.proxy import *
from m5.objects.System import System

class BaseModule(System):
    type = 'BaseModule'
    cxx_header = "disaggregated_component/base/base_module.hh"

    disaggregated_system = Param.DisaggregatedSystem(Parent.any,
        "The disaggregated system this module belongs to")
