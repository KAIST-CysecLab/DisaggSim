from m5.objects import *

def define_options(parser):
    """Parses commandline options for interconnect logic configuration"""

    parser.add_option("--d_logic_clock", type="string", default='2GHz')
    parser.add_option("--d_logic_latency", type="string", default='60ns')
    parser.add_option("--d_logic_rcv_limit", type="int", default=100)
    parser.add_option("--d_logic_snd_limit", type="int", default=100)


def create_d_logic(options, cid, rcv_trans, snd_trans, num_processing_units=1, logic_latency=None):
    """
    Create interconnect logic according to the given options, and returns it.
    """

    # Create interconnect logic
    d_logic = InterconnectLogic(
        is_require_rcv_trans = rcv_trans,
        is_require_snd_trans = snd_trans,
        num_processing_units = num_processing_units
    )

    # Setup voltage domain and clocks
    d_logic.clk_domain = SrcClockDomain()
    d_logic.clk_domain.clock = options.d_logic_clock
    d_logic.clk_domain.voltage_domain = VoltageDomain()

    # Assign the component ID
    d_logic.cid = cid

    # Configure the queues' limits
    d_logic.rcv_limit = options.d_logic_rcv_limit
    d_logic.snd_limit = options.d_logic_snd_limit

    if options.mem_size == None or options.mem_size == '0MB':
        # It's representing remote memory
        d_logic.range = AddrRange(options.d_mem_size)
    else:
        # It's representing local memory
        d_logic.range = AddrRange(options.mem_size, options.d_mem_size)

    # Configure interconnect logic's delay
    if logic_latency != None:
        d_logic.latency = logic_latency
    else:
        d_logic.latency = options.d_logic_latency

    return d_logic
