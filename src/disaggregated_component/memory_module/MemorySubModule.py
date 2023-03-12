from m5.params import *
from m5.proxy import *
from m5.objects.BaseModule import BaseModule


class MemorySubModule(BaseModule):
    type = 'MemorySubModule'
    cxx_header = "disaggregated_component/memory_module/memory_sub_module.hh"
    is_dummy = True
