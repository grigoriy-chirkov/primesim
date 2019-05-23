This is the README document for the PriME simulator.

PriME is an execution-driven x86 simulator for manycore architectures. It is capable of simulating 1000+ cores with rich cache hierarchies, coherence protocols and On-chip Network. It is capable of running both multi-threaded and multi-programmed workloads. Moreover, it can distribute multi-programmed workloads into multiple host machines and communicate via OpenMPI.

More details about PriME can be found in our ISPASS 2014 paper: Yaosheng Fu, David Wentzlaff, "PriME: A parallel and distributed simulator for thousand-core chips", IEEE International Symposium on Performance Analysis of Systems and Software (ISPASS), March 2014. 


License
-------
    
PriME uses the 3-clause BSD license as indicated in the LICENSE file. Since PriME also adopts the queue model from the MIT Graphite simulator, we also include the license file for Graphite in the home directory.

If you use PriME in your research please reference our ISPASS 2014 paper mentioned above and send us a citation of your work. 


Directory structure
-------------------

* bin: executable binary files 
* cmd: bash command files generated by run_prime
* dep: dependency files generated during compilation
* obj: object files generated during compilation
* output: default location for simulation report files
* src: source code files
* tools: python scripts to config and run PriME
* xml: default location for configuration xml files generated by config_prime


Requirements
------------

* gcc v4.6 or higher is recommended 
* Intel Pin tested under v2.14 71313
* OpenMPI tested under v1.8.4 (need to support MPI_THREAD_MULTIPLE which can be done by adding --enable-mpi-thread-multiple option during installation)
* libxml2 tested under v2.9.1
* PARSEC Benchmark Suite v3.0 (Only if you want to run PARSEC benchmarks)


How to compile
--------------

0. Modify env.sh to set those environment variables to be the correct paths.
0. Run source env.sh.
0. Run make. 


How to run
----------

0. Go to tools folder and set the configuration of the simulated system in the config_prime script.
0. Run config_prime, this will generate config.xml in the xml folder by default.
0. Run run_prime with your programs, this will generate config.out in the output folder by default. 
   
Note1: The details of configuration parameters are explained in the config_prime script itself.

Note2: Step 2 can be skipped by adding -r option in Step 3. The detail usage of each command can be viewed via -h option.

Note3: For PARSEC benchmarks, you need to also specify the thread count and input size for each program, the detailed format can be viewed via run_prime -h.
