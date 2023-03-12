from m5.params import *
from m5.proxy import *
from m5.objects.AbstractMemory import AbstractMemory

class ForwardMemory(AbstractMemory):
    type = 'ForwardMemory'
    cxx_header = "disaggregated_component/cpu_module/forward_mem.hh"

    internal_responder = ResponsePort("Responder")
    internal_requestor = RequestPort("Requestor")
    external_responder = ResponsePort("Responder")
    external_requestor = RequestPort("Requestor")