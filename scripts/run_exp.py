import subprocess
import numpy as np
import os
import os.path
import argparse
import json
from pandas.io.json import json_normalize
import pandas as pd
import threading
import time
import random
import pickle
import sys

parser = argparse.ArgumentParser()
parser.add_argument('-q', dest='qps', type=int, required=True)
parser.add_argument('-i', dest='init', action="store_true")
parser.add_argument('-m', dest='machine', type=str, required=True)
parser.add_argument('-n', dest='num_iter', type=int, required=True)
parser.add_argument('-d', dest='duration', type=int, required=True)
parser.add_argument('-f', dest='interfere', nargs='*')
parser.add_argument('-l', dest='interfere_level', type=int)

args = parser.parse_args()

qps = args.qps
machine = args.machine
num_iter = args.num_iter
init = args.init
duration = args.duration

# The folder to store the log files
# log_dir = "/home/yg397/filers/" + machine + "/root_cause/logs/chain_10/noise_injection/"
log_dir = "/home/ml2585/filers/" + machine + "/dependency/three_tier_grpc/"
# Application path 
# app_dir = "/home/yg397/Research/root_cause/applications/chain_10/"
app_dir = "/home/ml2585/applications/three_tier_grpc/"
# The path of cpu_intensive.py
# inter_dir = "/home/yg397/Research/root_cause/experiments/scripts/chain_10/"
inter_dir = "/home/ml2585/auto_microservices/scripts/noise_injection/"

os.system("mkdir -p " + log_dir)
if not os.path.exists(log_dir + "qps_" + str(qps)):
  os.mkdir(log_dir + "qps_" + str(qps))

file_id = int(len([lists for lists in os.listdir(log_dir + "qps_" + str(qps)) if os.path.isfile(os.path.join(log_dir + "qps_" + str(qps), lists))]) / 2)
print(file_id)

def write_csv(qps, i):
  json_file = log_dir + "qps_" + str(qps) + "/iter_" + str(i) + ".json"
  try:
    with open(json_file, 'r') as f:
      data = json.load(f)
      dfs = []
      for trace in data["data"]:
        dfs.append(json_normalize(trace["spans"])[["traceID","operationName", "startTime", "duration"]])
      df = pd.concat(dfs)
    csv_file = log_dir + "qps_" + str(qps) + "/iter_" + str(i) + ".csv"
    with open(csv_file, 'w') as f:
      df.to_csv(f)
    os.remove(json_file)
  except :
    print("Iteration", str(i), "failed")

n_services = 4

total_reqs = duration * qps
json_threads = []
pin_core_base = 30

for i in range(num_iter):
  if (init and i == 0) or (i % 50 == 0 and i != 0):
    cmd = "cd " + app_dir + " &&\n"
    cmd += "docker-compose down &&\n"
    cmd += "docker system prune --volumes -f &&\n"
    cmd += "docker-compose up -d &&\n"
    cmd += "sleep 3 &&\n"
    # Pin CPU cores to each service
    for s in range(n_services):
      cmd += "docker update three_tier_grpc_service_" + \
          str(s) + "_1 --cpuset-cpus " + str(pin_core_base + s) + " &&\n"
    cmd = cmd[:-3]
    # print(cmd)
    subprocess.run(cmd, shell=True)

  # interfere = np.random.binomial(1, 0.05, size=n_services)
  # print(interfere)
  interfere = [0, 0, 0, 0]
  if args.interfere:
    for s in args.interfere:
      interfere[int(s)] = 1
  print(interfere)
  
  cores_list = []
  
  for s in range(n_services):
    cores_list.append([pin_core_base + s])

  # The data that will be passed to metrics.py
  pass_data = {}
  pass_data["cores_list"] = cores_list
  pass_data["interfere"] = interfere
  pass_data["interfere_level"] = args.interfere_level

  # print(pass_data)

  with open("./pass_data_" + machine + ".pkl", "wb") as f:
    pickle.dump(pass_data, f)

  start_ts = int(time.time() * 1000000)
  cmd = app_dir + "wrk2/wrk -D exp -t10 -c500 -d" + str(duration) + " -L http://" + machine + ":9999 -R " + \
        str(qps) + " &\n"

  # Data collection
  cmd += "/usr/bin/python3 ./read_metrics.py -q " + str(qps) + " -m \"" + machine + "\" -d " + str(duration) + " -i " + str(file_id) + " -l " + log_dir + " &\n"

  cmd += "sleep 10 &&\n"

  # Launch interference jobs 
  for s in range(n_services):
    if interfere[s]:
      cmd += "taskset -c " + str(pin_core_base + s) + " python3 " + inter_dir + "cpu_intensive.py -i " + str(args.interfere_level)  + " -d " + str(10) + " &\n"

  cmd += "sleep 60"
  
  # print(cmd)
  subprocess.run(cmd, shell=True)
  end_ts = int(time.time() * 1000000) 

  cmd = "curl \"http://" + machine + ".ece.cornell.edu:16610/api/traces?service=service_0&limit=" + \
      str(total_reqs) + "&start=" + str(start_ts) + "&end=" + str(end_ts) + "\" > " + log_dir + "qps_" + str(qps)+ "/iter_" + str(file_id) + ".json"
  # print(cmd)
  subprocess.run(cmd, shell=True)


  t = threading.Thread(target=write_csv, args=(qps, file_id))
  json_threads.append(t)
  t.start()

for t in json_threads:
  t.join()

