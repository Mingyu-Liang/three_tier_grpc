import os
import sys

n_services = 4
n_iters = 5
interfere_level = [5, 10]

# for i in range(n_services):
#   for j in range(n_iters):
#     for k in interfere_level:
#       os.system("python3 run_exp.py -q100 -i -m ath-8 -n1 -d30 -f" + str(i) + " -l" + str(k))

# qps = [10, 50, 500, 1000]

qps = [1100, 1200, 1300, 1400, 1500]

for i in range(n_iters):
  for j in qps:
    os.system("python3 run_exp.py -q" + str(j) + " -i -m ath-8 -n1 -d30")