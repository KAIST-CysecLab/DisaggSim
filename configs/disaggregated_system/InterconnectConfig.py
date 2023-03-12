from m5.objects import *

def config_interconnect(options, d_system):

    # TODO-Disaggregated interconnect options
    d_interconnect = DisaggregatedInterconnect()
    d_interconnect.clk_domain = SrcClockDomain()
    d_interconnect.clk_domain.clock = '2GHz'
    d_interconnect.clk_domain.voltage_domain = VoltageDomain()
    d_interconnect.width = 64
    d_interconnect.frontend_latency = 3
    d_interconnect.forward_latency = 4
    d_interconnect.response_latency = 2

    for m in d_system.cpu_modules:
        m.d_logic.external_requestor = d_interconnect.cpu_side_ports
        m.d_logic.external_responder = d_interconnect.mem_side_ports

    # Currently memory pool is configured in MemModuleConfig.py
    for m in d_system.mem_modules:
        m.d_logic.external_responder = d_interconnect.mem_side_ports

    d_system.d_interconnect = d_interconnect
