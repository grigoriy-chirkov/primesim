import os
import sys

if not os.path.exists("jobs"):
    os.makedirs("jobs")

b_grouped = {0:["blackscholes", "bodytrack", "canneal"], 1:["dedup", "facesim"], 2:["ferret", "fluidanimate", "raytrace"], 3:["streamcluster", "swaptions"], 4:["vips", "x264"]}

curdir = os.getcwd()
g = int(sys.argv[1])


for n in [1,2,4,8,16,32,64]:
    for d in [1,2,4,8]:
        for t in ["2dmesh", "butterfly", "omega", "tree"]:
            with open("jobs/%s_g%d_n%d_d%d.slurm" % (t, g, n, d), "w+") as f:
                f.write("#!/bin/bash\n")
                f.write("#SBATCH --nodes=1\n")
                f.write("#SBATCH -B 1:4\n")
                f.write("#SBATCH --time=20:00:00\n")
                f.write("#SBATCH --mem=30000n")
                f.write("\n")
                f.write("cd %s\n"%curdir)
                f.write("source env.sh\n")
                f.write("\n")

                for b in b_grouped[g]:
                    f.write("run_prime -c %s/xml/%s_d%d.xml " % (curdir, t,d))
                    f.write("-o %s/output/%s_%s_n%d_d%d.out " % (curdir, b,t,n,d))
                    f.write("%s,%d,simsmall " % (b,n))
                    f.write("> %s/logs/%s_%s_n%d_d%d.log \n" % (curdir,b,t,n,d))
