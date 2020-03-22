import json
import os
import sys

def main():
  services = 4
  idx = 35
  core_num = 1
  for i in range(services):
    cmd = "docker update three_tier_grpc_service_" + str(i) + "_1 --cpuset-cpus "
    for j in range(core_num):
      cmd += str(idx + 1) + ','
      idx = idx + 1
    os.system(cmd[:-1])
  os.system("docker update three_tier_grpc_frontend_1 --cpuset-cpus " + str(idx + 1))

if __name__ == "__main__":
  main()