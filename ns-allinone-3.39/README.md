The network scenarios used in this work are implemented using the ns-3 simulator.  
These simulations generate the traffic traces and network conditions required for dataset creation.

---

## Build ns-3

Before running any scenario, ns-3 must be configured and built:

```bash
./ns3 configure
./ns3 build
```

## Run a single scenario 

A single simulation scenario can be executed using the following command:
```bash
./ns3 run scracth/automatedtop.cc
```
This script executes a predefined network topology and generates the corresponding simulation traces, which are later used for dataset construction and GNN-based performance prediction.