from m5.params import *
from m5.proxy import *
from m5.objects.XBar import BaseXBar


class DisaggregatedInterconnect(BaseXBar):
    type = 'DisaggregatedInterconnect'
    cxx_header = \
        "disaggregated_component/interconnect/xbar/disaggregated_interconnect.hh"