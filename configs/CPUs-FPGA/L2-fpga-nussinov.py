# -*- coding: utf-8 -*-
# Copyright (c) 2015 Jason Power
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Jason Power

""" This file creates a single CPU and a two-level cache system.
This script takes a single parameter which specifies a binary to execute.
If none is provided it executes 'hello' by default (mostly used for testing)

See Part 1, Chapter 3: Adding cache to the configuration script in the
learning_gem5 book for more information about this script.
This file exports options for the L1 I/D and L2 cache sizes.

IMPORTANT: If you modify this file, it's likely that the Learning gem5 book
           also needs to be updated. For now, email Jason <power.jg@gmail.com>

"""

# import the m5 (gem5) library created when gem5 is built
import m5
# import all of the SimObjects
from m5.objects import *

# Add the common scripts to our path
#m5.util.addToPath('../../')
m5.util.addToPath('../')
# import the caches which we made
from caches import *

# import the SimpleOpts module
from common import SimpleOpts

# Set the usage message to display
SimpleOpts.set_usage("usage: %prog [options] <binary to execute>")

# Finalize the arguments and grab the opts so we can pass it on to our objects
(opts, args) = SimpleOpts.parse_args()

# get ISA for the default binary to run. This is mostly for simple testing
isa = str(m5.defines.buildEnv['TARGET_ISA']).lower()

# Default to running 'hello', use the compiled ISA to find the binary
#binary = 'tests/test-progs/polybench-c-4.2/2mm_ref'
binary = 'tests/parallel/polybench-c-4.2/nussinov-fpga'
# Check if there was a binary passed in via the command line and error if
# there are too many arguments
if len(args) == 1:
    binary = args[0]
elif len(args) > 1:
    SimpleOpts.print_help()
    m5.fatal("Expected a binary to execute as positional argument")

# create the system we are going to simulate
system = System()

# Set the clock fequency of the system (and all of its children)
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '2GHz'
system.clk_domain.voltage_domain = VoltageDomain()


# Set up the system
system.mem_mode = 'timing'               # Use timing accesses
system.mem_ranges = [AddrRange('512MB')] # Create an address range

#system.piobus = IOXBar()

system.cpu = [TimingSimpleCPU(),TimingSimpleCPU(),TimingSimpleCPU(),TimingSimpleCPU() ]
system.fpga = [FpgaCPU()]#,FpgaCPU(),FpgaCPU()]
# Create an L1 instruction and data cache
system.cpu[0].icache = L1ICache(opts)
system.cpu[0].dcache = L1DCache(opts)
system.cpu[0].dcache.size = '16kB'
system.cpu[1].icache = L1ICache(opts)
system.cpu[1].dcache = L1DCache(opts)
system.cpu[1].dcache.size = '32kB'
system.cpu[2].icache = L1ICache(opts)
system.cpu[2].dcache = L1DCache(opts)
system.cpu[2].dcache.size = '32kB'
system.cpu[3].icache = L1ICache(opts)
system.cpu[3].dcache = L1DCache(opts)
system.cpu[3].dcache.size = '32kB'

# Connect the instruction and data caches to the CPU
system.topbus0 = SystemXBar()
system.topbus1 = SystemXBar()
system.topbus2 = SystemXBar()
system.topbus3 = SystemXBar()
system.topbus_c = SystemXBar()
system.cpu[0].icache.connectCPU(system.cpu[0])
system.cpu[0].dcache.cpu_side = system.topbus0.master
system.cpu[0].dcache_port = system.topbus0.slave
system.cpu[0].dtb.baseaddress_control_fpga = 0xffff00000000
system.cpu[0].itb.baseaddress_control_fpga = 0xffff00000000
system.cpu[0].dtb.fpga_bus_addr = 0xffff00000000
system.cpu[0].itb.fpga_bus_addr = 0xffff00000000
system.cpu[1].icache.connectCPU(system.cpu[1])
system.cpu[1].dcache.cpu_side = system.topbus1.master
system.cpu[1].dcache_port = system.topbus1.slave
system.cpu[1].dtb.baseaddress_control_fpga = 0xffff00000000
system.cpu[1].itb.baseaddress_control_fpga = 0xffff00000000
system.cpu[1].dtb.fpga_bus_addr = 0xffff00000000
system.cpu[1].itb.fpga_bus_addr = 0xffff00000000
system.cpu[2].icache.connectCPU(system.cpu[2])
system.cpu[2].dcache.cpu_side = system.topbus2.master
system.cpu[2].dcache_port = system.topbus2.slave
system.cpu[2].dtb.baseaddress_control_fpga = 0xffff00000000
system.cpu[2].itb.baseaddress_control_fpga = 0xffff00000000
system.cpu[2].dtb.fpga_bus_addr = 0xffff00000000
system.cpu[2].itb.fpga_bus_addr = 0xffff00000000
system.cpu[3].icache.connectCPU(system.cpu[3])
system.cpu[3].dcache.cpu_side = system.topbus3.master
system.cpu[3].dcache_port = system.topbus3.slave
system.cpu[3].dtb.baseaddress_control_fpga = 0xffff00000000
system.cpu[3].itb.baseaddress_control_fpga = 0xffff00000000
system.cpu[3].dtb.fpga_bus_addr = 0xffff00000000
system.cpu[3].itb.fpga_bus_addr = 0xffff00000000


system.fpga[0].clk_domain = SrcClockDomain(clock='150MHz')
system.fpga[0].clk_domain.voltage_domain = VoltageDomain()
system.fpga[0].fpga_bus_addr = 0xffff00000000
system.fpga[0].size_control_fpga = 72
system.fpga[0].ModuleName = 'same_scratchpad_allocated/nussinov/obj_dir/Vour'
system.fpga[0].control_port = system.topbus_c.master 

#system.fpga[1].clk_domain = SrcClockDomain(clock='150MHz')
#system.fpga[1].clk_domain.voltage_domain = VoltageDomain()
#system.fpga[1].fpga_bus_addr = 0xffff00000000+10*8
#system.fpga[1].size_control_fpga = 72
#system.fpga[1].ModuleName = '2mm/obj_dir/Vour'
#system.fpga[1].control_port = system.topbus_c.master 

#system.fpga[2].clk_domain = SrcClockDomain(clock='150MHz')
#system.fpga[2].clk_domain.voltage_domain = VoltageDomain()
#system.fpga[2].fpga_bus_addr = 0xffff00000000+20*8
#system.fpga[2].size_control_fpga = 72
#system.fpga[2].ModuleName = '2mm/obj_dir/Vour'
#system.fpga[2].control_port = system.topbus_c.master 

#system.fpga[0].show_address = 1

system.topbus_c.slave = system.topbus0.master
system.topbus_c.slave = system.topbus1.master
system.topbus_c.slave = system.topbus2.master
system.topbus_c.slave = system.topbus3.master

system.l2bus = L2XBar()
#system.fpga[0].icache_port = system.l2bus.slave
system.fpga[0].dcache_port = system.l2bus.slave
system.fpga[0].ACP = 1
#system.fpga[1].dcache_port = system.l2bus.slave
#system.fpga[2].dcache_port = system.l2bus.slave

# Create a memory bus, a coherent crossbar, in this case


# Hook the CPU ports up to the l2bus
system.cpu[0].icache.connectBus(system.l2bus)
system.cpu[0].dcache.connectBus(system.l2bus)
system.cpu[1].icache.connectBus(system.l2bus)
system.cpu[1].dcache.connectBus(system.l2bus)
system.cpu[2].icache.connectBus(system.l2bus)
system.cpu[2].dcache.connectBus(system.l2bus)
system.cpu[3].icache.connectBus(system.l2bus)
system.cpu[3].dcache.connectBus(system.l2bus)
# Create an L2 cache and connect it to the l2bus
system.l2cache = L2Cache(opts)
system.l2cache.connectCPUSideBus(system.l2bus)
system.l2cache.size = '64kB'
system.l2cache.tags = PART_LRU()
system.l2cache.tags.address_begin = 28960
system.l2cache.tags.address_end   = 160028
# Create a memory bus
system.membus = SystemXBar()

# Connect the L2 cache to the membus
system.l2cache.connectMemSideBus(system.membus)

# create the interrupt controller for the CPU
system.cpu[0].createInterruptController()
system.cpu[1].createInterruptController()
system.cpu[2].createInterruptController()
system.cpu[3].createInterruptController()

# For x86 only, make sure the interrupts are connected to the memory
# Note: these are directly connected to the memory bus and are not cached
if m5.defines.buildEnv['TARGET_ISA'] == "x86":
    system.cpu[0].interrupts[0].pio = system.membus.master
    system.cpu[0].interrupts[0].int_master = system.membus.slave
    system.cpu[0].interrupts[0].int_slave = system.membus.master
    system.cpu[1].interrupts[0].pio = system.membus.master
    system.cpu[1].interrupts[0].int_master = system.membus.slave
    system.cpu[1].interrupts[0].int_slave = system.membus.master
    system.cpu[2].interrupts[0].pio = system.membus.master
    system.cpu[2].interrupts[0].int_master = system.membus.slave
    system.cpu[2].interrupts[0].int_slave = system.membus.master
    system.cpu[3].interrupts[0].pio = system.membus.master
    system.cpu[3].interrupts[0].int_master = system.membus.slave
    system.cpu[3].interrupts[0].int_slave = system.membus.master
# Connect the system up to the membus
system.system_port = system.membus.slave

# Create a DDR3 memory controller
system.mem_ctrl = DDR3_1600_x64()
system.mem_ctrl.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.master

# Create a process for a simple "Hello World" application
process = LiveProcess()
# Set the command
# cmd is a list which begins with the executable (like argv)
process.cmd = [binary]
# Set the cpu to use the process as its workload and create thread contexts
system.cpu[0].workload = process
system.cpu[0].createThreads()

process1 = LiveProcess()
process1.cmd = ['tests/parallel/polybench-c-4.2/jacobi-2d_ref']
system.cpu[1].workload = process1
system.cpu[1].createThreads()

process2 = LiveProcess()
process2.cmd = ['tests/parallel/polybench-c-4.2/jacobi-2d_ref']
system.cpu[2].workload = process2
system.cpu[2].createThreads()

process3 = LiveProcess()
process3.cmd = ['tests/parallel/polybench-c-4.2/jacobi-2d_ref']
system.cpu[3].workload = process3
system.cpu[3].createThreads()
# set up the root SimObject and start the simulation
root = Root(full_system = False, system = system)
# instantiate all of the objects we've created above
m5.instantiate()

print "Beginning simulation!"
exit_event = m5.simulate()
print 'Exiting @ tick %i because %s' % (m5.curTick(), exit_event.getCause())
