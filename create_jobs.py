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
                f.write("#SBATCH --mem=30000\n")
                f.write("#SBATCH -C skylake\n")
                f.write("#SBATCH --time=20:00:00\n")
                f.write("#SBATCH --mail-type=begin\n")
                f.write("#SBATCH --mail-type=end\n")
                f.write("#SBATCH --mail-user=gchirkov@princeton.edu\n")
                f.write("\n")
                f.write("cd %s\n"%curdir)
                f.write("source env.sh\n")
                f.write("\n")

                f.write("if [ ! -d \"%s/../parsec_data\"  ]; then\n" % curdir)
                f.write("\tmkdir %s/../parsec_data\n" % curdir)
                f.write("fi\n")
                f.write("\n")

                f.write("if [ ! -d \"%s/../parsec_data/%s_n%d_d%d\"  ]; then\n" % (curdir, t,n,d))
                f.write("\tcp -r %s/../parsec_data_template  %s/../parsec_data/%s_n%d_d%d\n" % (curdir, curdir, t,n,d))
                f.write("fi\n")


                for b in b_grouped[g]:
                    f.write("run_prime -c %s/xml/%s_d%d.xml "  % (curdir, t,d))
                    f.write("-o %s/output/%s_%s_n%d_d%d.out "  % (curdir, b,t,n,d))
                    f.write("-d %s/../parsec_data/%s_n%d_d%d " % (curdir, t,n,d))
                    if n == 1:
                        f.write("%s,%d,simsmall " % (b,0))
                    else:
                        f.write("%s,%d,simsmall " % (b,n))
                    f.write("> %s/logs/%s_%s_n%d_d%d.log \n"   % (curdir,b,t,n,d))
