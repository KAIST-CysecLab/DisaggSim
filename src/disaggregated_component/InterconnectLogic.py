from m5.params import *
from m5.proxy import *
from m5.objects.ClockedObject import ClockedObject


class InterconnectLogic(ClockedObject):
    type = 'InterconnectLogic'
    cxx_header = "disaggregated_component/interconnect_logic.hh"

    module = Param.BaseModule(Parent.any,
        "The module this interconnect logic belongs to")

    internal_responder = ResponsePort("Response Port")
    internal_requestor = RequestPort("Request port")
    external_responder = ResponsePort("Response Port")
    external_requestor = RequestPort("Request port")

    cid = Param.Int("The component identifier of this module")

    rcv_limit = Param.Int(100, "Logic recieve queue limit")
    snd_limit = Param.Int(100, "Logic recieve queue limit")

    range = Param.AddrRange('0B',"mem range to receive")

    latency = Param.Latency('60ns', "Logic latency")

    is_require_rcv_trans = Param.Bool("Is receive translation requried or not")
    is_require_snd_trans = Param.Bool("Is send translation requried or not")

    num_processing_units = Param.Int(1, "The number of processing units")



