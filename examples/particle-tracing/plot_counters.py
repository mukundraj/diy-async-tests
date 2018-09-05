'''
Python script for plotting number of times iexchange callback is called and number of 
rounds needed for all particles to be fully traced with synchronous DIY. This script 
required output files generated by the following commands:

./NEK_TEST1 &> output_nek_iex.txt   // executable ./ptrace built with IEXCHANGE=1 in ptrace.cpp
./NEK_TEST1 &> output_nek_isyn.txt  // executable ./ptrace built with IEXCHANGE=0 in ptrace.cpp

'''

import matplotlib
matplotlib.use('TkAgg')
import matplotlib.pyplot as plt

# paths to output files generated using ./NEK_TEST1 with IEXCHANGE=1 and IEXCHANGE=0
opfile_iex = "./output_nek_iex.txt"
opfile_syn = "./output_nek_syn.txt"


blocks = [4, 16, 64, 256]

# read and parse the iexchange output file
with open(opfile_iex, "r") as fi:
    comm_counts_iex = []
    for ln in fi:
        if ln.startswith("resultline"):
            sp = ln.rstrip().split(',')
            comm_counts_iex.append(int(sp[4]))


# read and parse the synchronous output file
with open(opfile_syn, "r") as fi:
    comm_counts_syn = []
    for ln in fi:
        if ln.startswith("resultline"):
            sp = ln.rstrip().split(',')
            comm_counts_syn.append(int(sp[5]))


# plot results
plt.plot(blocks, comm_counts_iex, 's', linestyle='dotted', label='iexchange Iproxy')
plt.plot(blocks, comm_counts_syn, 'D', linestyle='dotted', label='iexchange proxy')
plt.yscale('log')
plt.xlabel('number of blocks')
plt.ylabel('communication calls')
plt.title('# communication calls versus blocks for nek5000 data')
ax = plt.gca()
plt.legend(loc='best')

plt.show()