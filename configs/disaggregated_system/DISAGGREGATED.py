import m5
from m5.objects import *
from m5.defines import buildEnv
from m5.util import addToPath

def define_options(parser):
    return

def create_topology(controllers, options):
    exec("import topologies.%s as Topo" % options.topology)
    topology = eval("Topo.%s(controllers)" % options.topology)
    return topology

def create_system(options, ruby_system, num_sequencers):
    if buildEnv['PROTOCOL'] != 'DISAGGREGATED':
        panic("This script requires Garnet_standalone protocol to be built.")

    module_sequencers = []
    module_controllers = []

    #
    # Must create the individual controllers before the network to ensure the
    # controller constructors are called before the network constructor
    #
    for i in range(num_sequencers):
        # TODO
        # Create module sequencers
        logic_clock = SrcClockDomain(clock = options.d_logic_clock,
                                        voltage_domain = VoltageDomain())
        module_sequencer = ModuleSequencer(ruby_system = ruby_system)
        module_sequencer.clk_domain = logic_clock

        # Generate module controllers
        # version == component id
        module_controller = DisaggregatedModule_Controller(version = i, ruby_system = ruby_system)
        module_controller.clk_domain = logic_clock

        # Assign sequencer to the controller
        module_controller.module_sequencer = module_sequencer

        # Generate MessageBuffers
        module_controller.requestToModule = MessageBuffer(ordered = True, queuing_delay = options.queuing_delay)
        module_controller.requestToModule.out_port = ruby_system.network.in_port
        module_controller.responseToModule = MessageBuffer(ordered = True, queuing_delay = options.queuing_delay)
        module_controller.responseToModule.out_port = ruby_system.network.in_port
        module_controller.requestFromModule = MessageBuffer(ordered = True, queuing_delay = options.queuing_delay)
        module_controller.requestFromModule.in_port = ruby_system.network.out_port
        module_controller.responseFromModule = MessageBuffer(ordered = True, queuing_delay = options.queuing_delay)
        module_controller.responseFromModule.in_port = ruby_system.network.out_port
        module_controller.requestToModuleAck = MessageBuffer(ordered = True, queuing_delay = options.queuing_delay)
        module_controller.requestToModuleAck.in_port = ruby_system.network.out_port
        module_controller.responseToModuleAck = MessageBuffer(ordered = True, queuing_delay = options.queuing_delay)
        module_controller.responseToModuleAck.in_port = ruby_system.network.out_port
        module_controller.requestFromModuleAck = MessageBuffer(ordered = True, queuing_delay = options.queuing_delay)
        module_controller.requestFromModuleAck.out_port = ruby_system.network.in_port
        module_controller.responseFromModuleAck = MessageBuffer(ordered = True, queuing_delay = options.queuing_delay)
        module_controller.responseFromModuleAck.out_port = ruby_system.network.in_port
        module_controller.requestToModuleReplayBuffer = ReplayBuffer(ordered = True, queuing_delay = options.queuing_delay)
        module_controller.responseToModuleReplayBuffer = ReplayBuffer(ordered = True, queuing_delay = options.queuing_delay)

        module_controller.responseFromMemory = MessageBuffer(ordered = True, queuing_delay = options.queuing_delay)
        module_controller.mandatoryQueue = MessageBuffer(ordered = True, queuing_delay = options.queuing_delay)
        # Init sequence counter
        module_controller.reqeustToSeqCounter = 0
        module_controller.responseToSeqCounter = 0
        module_controller.requestFromSeqCounter = -1
        module_controller.responseFromSeqCounter = -1

        # Append sequencer and controller to the lists
        module_sequencers.append(module_sequencer)
        module_controllers.append(module_controller)


    ruby_system.network.number_of_virtual_networks = 2
    topology = create_topology(module_controllers, options)
    return (module_sequencers, module_controllers, topology)
