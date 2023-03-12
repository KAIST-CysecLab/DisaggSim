from m5.params import *
from m5.proxy import *
from m5.objects.BaseModule import BaseModule


class MemoryModule(BaseModule):
    type = 'MemoryModule'
    cxx_header = "disaggregated_component/memory_module/memory_module.hh"
    is_dummy = True
