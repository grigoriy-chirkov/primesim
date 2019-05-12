import os


for d in [1,2,4,8]:
    for t in ["2dmesh", "butterfly", "omega", "tree"]:
        os.system("config_prime -n 256 -c %d -d %d -t %s -o xml/%s_d%d.xml" % (2**20, d, t, t, d))


