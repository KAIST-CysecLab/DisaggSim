from m5.params import *
from m5.proxy import *
from m5.objects.BaseModule import BaseModule

class CPUModule(BaseModule):
    type = 'CPUModule'
    cxx_header = "disaggregated_component/cpu_module/cpu_module.hh"
