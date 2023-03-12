## About The Project

DisaggSim (<ins>Disagg</ins>regated architecture <ins>Sim</ins>ulator) provides a scalable and configurable simulation for disaggregated architecture based on the gem5 simulator.

## Citation

[Paper](https://www.sciencedirect.com/science/article/pii/S1569190X23000217)
[BibTex](https://scholar.googleusercontent.com/scholar.bib?q=info:Qj31ovrIvM8J:scholar.google.com/&output=citation&scisdr=CgUOhm_6EL_Q1JE7E2A:AAGBfm0AAAAAZAg9C2DtESJcInRsKH8NOdREXzoyYoWz&scisig=AAGBfm0AAAAAZAg9C32xMGH9RAXFQUUaqXgOhysrkK8s&scisf=4&ct=citation&cd=-1&hl=ko)

## Get Started
Please refer to the original gem5's documentation if you need to install the dependencies.

### Basic Setup (compilation + simulation)
```
$ scons build/X86/gem5.opt -j$(nproc)
$ build/X86/gem5.opt configs/disaggregated_system/disaggregated_se.py --cmd="tests/test-progs/hello/bin/x86/linux/hello" --cpu-type=TimingSimpleCPU --caches --l2cache --l3cache
```

### With Garnet (compilation + simulation)
```
$ scons build/DISAGGREGATED/gem5.opt --default=X86 PROTOCOL=DISAGGREGATED  -j$(nproc)
$ build/X86/gem5.opt configs/disaggregated_system/disaggregated_se.py --cmd="tests/test-progs/hello/bin/x86/linux/hello" --cpu-type=TimingSimpleCPU --d_ruby --network=garnet --caches --l2cache --l3cache
```

### Simulation Script Example (Baseline)
```sh
GEM=<GEM5 DIRECTORY>/build/DISAGGREGATED/gem5.opt
SE=<GEM5 DIRECTORY>/configs/disaggregated_system/disaggregated_se.py

$GEM $SE\
    --cmd=<command-to-run> \
    --options=<options-for-cmd> \
    --cpu-type=DerivO3CPU \
    --caches --l2cache --l3cache \
    --network=garnet --d_ruby 
```

### Options (In-progress)

**CPU Module-related Options**
- `cpu-type`: the CPU type
    - choose from 'AtomicSimpleCPU', 'NonCachingSimpleCPU', 'X86KvmCPU', 'DerivO3CPU', 'TraceCPU', 'TimingSimpleCPU')
- `num_cpus`: the number of cores within a module (default: 1)
    - e.g. `--num-cpus = 1`
    - also need to append cmd with ';new command' (e.g. `--cmd="/bin/x86/linux/hello;/bin/x86/linux/hello"`)
- `num_cpu_modules`: the number of CPU Modules (default: 2)
    - e.g. `--num_cpu_modules=2`
- `allocated_d_mem_size`: allocated remote memory (default: "1024MB")
    - e.g. `--allocated_d_mem_size = "1024MB"`
- `local_mem_size`: indicate that local memory is used and set how large it is
    - e.g. `--local_mem_size="1024MB"`
- `local_mem_type`: local memory type (default: "DDR4_2400_16x4")
    - e.g. `--local_mem_size="DDR4_2400_16x4"`
- `cpu-clock`: CPU core clock(default: '3.4GHz')
- `sys-clock`: module clock (e.g. internal interconnect clock) (default: '3.4GHz')
- `caches`: indicate that (private) l1 cache is used (default: None)
- `l1d_size`: l1d cache size (default:'32kB')
- `l1i_size`: l1i cache size (default:'32kB') 
- `l1d_assoc`: l1d set-associativity (default:'8')
- `l1i_assoc`: l1i set-associativity (default:'8')
- `l2cache`: indicate that (private) l2 cache is used (default: None)
- `l2_size`: l2 cache size (default:'256kB')
- `l2_assoc`: l2 set-associativity (default:'8')
- `l3cache`: indicate that (shared) l3 cache is used (default: None)
- `l3_size`: l3 cache size per core (default:'2MB')
    - NOTE: number of cache set must be a power of 2
- `l3_assoc`: l3 set-associativity (default:'16')

**Memory Module-related Options**
- `d_logic_mem_pool_latency`: the latency applied to the logic attached to memory pool (default='40ns')
- `num_mem_ctrls`: the number of memory controllers per one CPU module (integer, default=2)
- `num_mem_modules`: the number of memory modules (e.g., DRAM) per one memory controller (integer, default=2)
- `size_mem_module`: the size of one memory module (unit: MB) (integer, default=256)
- `d_mem_size`: the size of remote memory per one CPU (string, default=1024MB)
    - MUST be the same as `options.num_mem_ctrls * options.num_mem_modules * options.size_mem_module`
    - MUST be the same or greater than `allocated_d_mem_size`
    - both sanity checks are done in `configs/disaggregated_system/MemModuleConfig.py`
- NOTE: currently, components' mapping information is hard-coded in `src/disaggregated_component/disaggregated_system.cc`
    - Memory Pool Manager's CID is the last CPU's CID + 1 (also fixed in `configs/disaggregated_system/MemModuleConfig.py`)
    - Address translation tables are generated with the fixed CID information.
- `mem_interleave`: if specified, memory controllers' are accessed with 4KB-page interleaving. If not, CPU modules' interconnect logics are configured to translate addresses without interleaving. (default=False)
- `interleave_unit`: memory interleaving unit (byte) (integer, default=4096)
- `num_mem_pool_logic_processing_units`: number of processing units in the interconnect logic of a memory pool (integer, default=1)

**Interconnect Logic-related Options**
- `d_logic_clock`: interconnect logic clock frequency (default: '2GHz')
    - e.g. `--d_logic_clock='2GHz'`
- `d_logic_latency`: interconnect logic latency (default: '60ns')
    - e.g. `--d_logic_latency='60ns'`
    - pipelining could be done by modifying latencies in (processSend & processReceive) but for now, packets are processed in a one-by-one manner

**Interconnect-related Options**
- `d_ruby`: indicate that garnet network is used (default: None); if Garnet is not used, just a default NonCoherentXBar is used
    - e.g. `--d_ruby`
- `d_ruby_clock`: ruby system's clock (default: '2GHz')
    - e.g. `--d_ruby = '2GHz'`


### Gem5 Tips
- Visualize the current configuration with pydot (output pdf file created in m5out directory)
```
$ pip install pydot
$ sudo apt-get install graphviz
$ <command to run the simulation>
```

## License

As DisaggSim is based on the gem5 simulator, it is released under a Berkeley license.

## Reference
[The original gem5 paper](https://dl.acm.org/doi/10.1145/2024716.2024718)
[The gem5 simulator system](https://www.gem5.org/)
