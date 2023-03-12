from m5.params import *
from m5.SimObject import SimObject

class DisaggregatedSystem(SimObject):
    type = 'DisaggregatedSystem'
    cxx_header = "disaggregated_component/disaggregated_system.hh"

    translation_config = Param.String(
        ".",
        "The path to the translation info config file"
    )
    location_map_config = Param.String(
        ".",
        "The path to location mapping config file"
    )

    num_cpu_modules = Param.Unsigned(
        2,
        "The number of total CPU modules in the disaggregated system"
    )

    num_mem_ctrls = Param.Unsigned(
        2,
        "The number of memory controllers per one CPU module"
    )

    size_per_mem_ctrl = Param.Unsigned(
        512 * (1<<20),
        "Physical memory space owned by one memory controller (Unit: Byte)"
    )

    mem_interleave = Param.Bool(
        False,
        "Memory controller interleaving (4KB-page)"
    )

    interleave_unit = Param.Unsigned(
        4096,
        "Memory controller interleaving unit (byte)"
    )
