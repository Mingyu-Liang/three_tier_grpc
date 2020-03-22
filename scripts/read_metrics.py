import subprocess
import argparse
import pandas as pd
import time
import pickle
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument('-q', dest='qps', type=int, required=True)
parser.add_argument('-d', dest='duration', type=int, required=True)
parser.add_argument('-m', dest='machine', type=str, required=True)
parser.add_argument('-i', dest='iteration', type=str, required=True)
parser.add_argument('-l', dest='log_dir', type=str, required=True)

args = parser.parse_args()

qps = args.qps
duration = args.duration
machine = args.machine
iteration = args.iteration
log_dir = args.log_dir

with open("./pass_data_" + machine + ".pkl", "rb") as f:
  pass_data = pickle.load(f)

cores_list = pass_data["cores_list"]
interfere = pass_data["interfere"]
interfere_level = pass_data["interfere_level"]

# number of services
n_services = 4

prefix = "three_tier_grpc_"
def remove_prefix(text, prefix):
  if text.startswith(prefix):
    return text[len(prefix):]
  return text

def extract_name(raw_name):
  raw_name = remove_prefix(raw_name, "\"table ")
  raw_name = raw_name[:-2]
  return remove_prefix(raw_name, prefix)

def extract_cpu_util(raw_cpu_util):
  return float(raw_cpu_util[:-1])

def extract_mem_util(raw_mem_util):
  return float(raw_mem_util[:-1])

def extract_netio(raw_netio):
  rd, wr = raw_netio.split(" / ")
  rd_kb = 0
  wr_kb = 0
  if rd.endswith("kB"):
    rd_kb = float(rd[:-2])
  elif rd.endswith("MB"):
    rd_kb = float(rd[:-2]) * 2**10
  elif rd.endswith("GB"):
    rd_kb = float(rd[:-2]) * 2**20
  elif rd.endswith("B"):
    rd_kb = float(rd[:-1]) / 2**10
  if wr.endswith("kB"):
    wr_kb = float(wr[:-2])
  elif wr.endswith("MB"):
    wr_kb = float(wr[:-2]) * 2**10
  elif wr.endswith("GB"):
    wr_kb = float(wr[:-2]) * 2**20
  elif wr.endswith("B"):
    wr_kb = float(wr[:-1]) / 2**10
  return rd_kb, wr_kb

def extract_blkio(raw_blkio):
  raw_blkio = raw_blkio[:-1]
  rd, wr = raw_blkio.split(" / ")
  rd_kb = 0
  wr_kb = 0
  if rd.endswith("kB"):
    rd_kb = float(rd[:-2])
  elif rd.endswith("MB"):
    rd_kb = float(rd[:-2]) * 2**10
  elif rd.endswith("GB"):
    rd_kb = float(rd[:-2]) * 2**20
  elif rd.endswith("B"):
    rd_kb = float(rd[:-1]) / 2**10
  if wr.endswith("kB"):
    wr_kb = float(wr[:-2])
  elif wr.endswith("MB"):
    wr_kb = float(wr[:-2]) * 2**10
  elif wr.endswith("GB"):
    wr_kb = float(wr[:-2]) * 2**20
  elif wr.endswith("B"):
    wr_kb = float(wr[:-1]) / 2**10
  return rd_kb, wr_kb

def read_stats():
  start_timestamp = int(round(time.time()))
  # metrics_df = pd.DataFrame(columns=["interfere", "name", "cpu_util", "core_util", "num_cores", "mem_util", "netio_rd", "netio_wr", "blkio_rd", "blkio_wr", "timestamp"])
  metrics_df = pd.DataFrame(columns=["name", "interfere", "interfere_level", "core_util", "num_cores", "timestamp"])

  while int(round(time.time())) - start_timestamp <= duration:
    mpstat = subprocess.Popen(('mpstat', '-P', 'ALL', '1', '1'), stdout=subprocess.PIPE)
    tail = subprocess.Popen(('tail', '-n', '+5'), stdout=subprocess.PIPE, stdin=mpstat.stdout)
    mpstat_out = subprocess.check_output(('awk', '{print (100-$13)}'), stdin=tail.stdout)

    # docker_out = subprocess.check_output(["docker", "stats", "--no-stream", "--format", 
    #     "\"table {{.Name}}\t{{.CPUPerc}}\t{{.MemPerc}}\t{{.NetIO}}\t{{.BlockIO}}\""]).decode("utf-8")

    
    mpstat.wait()
    mpstat_list = mpstat_out.decode("utf-8").split('\n')
    timestamp = int(round(time.time()))

    for s in range(n_services):
      n_cores = float(len(cores_list[s]))
      core_util = 0
      count = 0
      for c in cores_list[s]:
        count += 1.0
        core_util += float(mpstat_list[c])
      core_util /= count

      if interfere[s]:
        if_interfere = 1.0
        if_level = interfere_level
      else:
        if_interfere = 0.0
        if_level = 0.0

      data_dict = {
        "name": "service_" + str(s), 
        "core_util": core_util,
        "num_cores": n_cores,
        "interfere": if_interfere,
        "timestamp": timestamp, 
        "interfere_level": if_level
      }

      metrics_df = metrics_df.append(data_dict, ignore_index=True)

    
    # for line in docker_out.split("\n"):
    #   if line.startswith("\"table"):
    #     fields = line.split("\t")

    #     name = extract_name(fields[0])
    #     if not name.startswith("service"):
    #       continue
    #     cpu_util = extract_cpu_util(fields[1])
    #     mem_util = extract_mem_util(fields[2])
    #     netio_rd, netio_wr = extract_netio(fields[3])
    #     blkio_rd, blkio_wr = extract_blkio(fields[4])

    #     s = int(name.split('_')[-1])
    #     n_cores = float(len(cores_list[s]))
    #     core_util = 0
    #     count = 0
    #     for c in cores_list[s]:
    #       count += 1.0
    #       core_util += float(mpstat_list[c])
    #     core_util /= count
        
    #     if interfere[s]:
    #       if_interfere = 1.0
    #     else:
    #       if_interfere = 0.0

    #     data_dict = {
    #       "name": name, 
    #       "cpu_util": cpu_util, 
    #       "core_util": core_util,
    #       "num_cores": n_cores,
    #       "interfere": if_interfere,
    #       "mem_util": mem_util, 
    #       "netio_rd": netio_rd, 
    #       "netio_wr": netio_wr, 
    #       "blkio_rd": blkio_rd, 
    #       "blkio_wr": blkio_wr, 
    #       "timestamp": timestamp,
          
    #     }

    #     metrics_df = metrics_df.append(data_dict, ignore_index=True)

  # metrics_df = metrics_df.groupby(["name"], as_index=False).agg(
  #   {
  #     "timestamp": np.ptp,
  #     "cpu_util": np.mean, 
  #     "core_util": np.mean, 
  #     "mem_util": np.mean, 
  #     "num_cores": np.mean, 
  #     "interfere": np.mean,
  #     "netio_rd": np.ptp, 
  #     "netio_wr": np.ptp, 
  #     "blkio_rd": np.ptp, 
  #     "blkio_wr": np.ptp, 
      
  #   }
  # )

  # metrics_df = metrics_df.groupby("name").agg(np.mean)
  
  # metrics_df["netio_rd"] = metrics_df["netio_rd"] / metrics_df["timestamp"]
  # metrics_df["netio_wr"] = metrics_df["netio_wr"] / metrics_df["timestamp"]
  # metrics_df["blkio_rd"] = metrics_df["blkio_rd"] / metrics_df["timestamp"]
  # metrics_df["blkio_wr"] = metrics_df["blkio_wr"] / metrics_df["timestamp"]
  # metrics_df["blkio_wr"] = metrics_df["blkio_wr"] / metrics_df["timestamp"]

  # metrics_df = metrics_df[["interfere", "num_cores", "cpu_util", "core_util", "mem_util", "netio_rd", "netio_wr", "blkio_rd", "blkio_wr"]]
  
  with open(log_dir + "qps_" + str(qps) + "/metrics_" + str(iteration) + ".csv", "w") as f:
    metrics_df.to_csv(f)

read_stats()