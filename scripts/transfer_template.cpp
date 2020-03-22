#include <thrift/concurrency/ThreadManager.h>
#include <thrift/concurrency/PlatformThreadFactory.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/server/TServer.h>
#include <thrift/server/TThreadPoolServer.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/server/TNonblockingServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/transport/TNonblockingServerSocket.h>
#include <thrift/TToString.h>
#include <thrift/stdcxx.h>
#include <yaml-cpp/yaml.h>
#include "../tracing.h"
#include <jaegertracing/Tracer.h>
#include <opentracing/propagation.h>
#include "../ClientPool.h"
#include "../ThriftClient.h"
#include "../logger.h"

#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <fstream>
#include <getopt.h>
#include <thread>
#include <future>
#include <execinfo.h>
#include <string>
#include <random>
#include <time.h>

/*[[[cog
import cog
import json
import math
from enum import Enum

def insert_list(rpc_send, rpcs_send_list, rpcs_send_dep):
  if rpc_send in rpcs_send_list:
    return rpcs_send_list.index(rpc_send)
  if len(rpcs_send_dep[rpc_send]) == 0:
    rpcs_send_list.insert(0, rpc_send)
    return 0
  else:
    dep_index_max = 0
    for j in rpcs_send_dep[rpc_send]:
      dep_index = insert_list(j, rpcs_send_list, rpcs_send_dep)
    for j in rpcs_send_dep[rpc_send]:
      if rpcs_send_list.index(j) > dep_index_max:
        dep_index_max = rpcs_send_list.index(j)
    rpcs_send_list.insert(dep_index_max + 1, rpc_send)
    return dep_index_max + 1

with open("../config/rpcs.json",'r') as load_rpcs_f:
  rpcs_json = json.load(load_rpcs_f)

with open("../config/paths.json",'r') as load_paths_f:
  paths_json = json.load(load_paths_f)
  edges = paths_json[pathName]["edges"]
  dependency = paths_json[pathName]["dependency"]

with open("../config/services.json",'r') as load_services_f:
  services_json = json.load(load_services_f)
  children_services = []
  for i in services_json[serviceName]['rpcs_send']:
    children_services.append(rpcs_json["rpc_" + str(i)]["server"])
  for i in children_services:
    cog.outl('#include "../../gen-cpp/' + i + '.h"')
  cog.outl('#include "../../gen-cpp/' + serviceName + '.h"')

]]]*/
//[[[end]]]

using namespace std;
using namespace apache::thrift;
using namespace apache::thrift::concurrency;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
using namespace apache::thrift::server;
using json = nlohmann::json;
using std::chrono::microseconds;
using std::chrono::duration_cast;
using std::chrono::system_clock;

using namespace auto_microservices;

enum DistributionType {
  constant,
  log_normal
};

struct rpc_params {
  DistributionType distribution_type;
  double pre_time_mean;
  double pre_time_std;
  double post_time_mean;
  double post_time_std;
  double proc_time_mean;
  double proc_time_std;
};

/*[[[cog
import cog

use_loop = 1 # 0 for bubble sort, 1 for busy waiting, 2 for sleep
use_pre = False
if use_loop == 0:
  cog.outl('void bubble_sort(int *a, int len){')
  cog.outl('int max = len-1;')
  cog.outl('int i, j;')
  cog.outl('for(i=0;i<max;i++){')
  cog.outl('for(j=0;j<max-i;j++){')
  cog.outl('if(a[j+1]<a[j]){')
  cog.outl('int t = a[i];')
  cog.outl('a[i] = a[j];')
  cog.outl('a[j] = t; }}}}')

rpcs_receive = services_json[serviceName]['rpcs_receive']
cog.outl('class ' + serviceName + 'Handler: public ' + serviceName + 'If {')
cog.outl('private:')
cog.outl('std::default_random_engine _gen;')
cog.outl('std::lognormal_distribution<double> _dist;')
cog.outl('double _scaler;')
if len(children_services) > 0:
  for children_service in children_services:
    cog.outl('ClientPool<ThriftClient<' + children_service + 'Client>> *_' + children_service + '_client_pool;')
for i in services_json[serviceName]['rpcs_receive']:
  cog.outl('rpc_params * _rpc_' + str(i) + '_params;')
  cog.outl('std::lognormal_distribution<double> _rpc_' + str(i) + '_proc_dist;')
for i in services_json[serviceName]['rpcs_send']:
  cog.outl('rpc_params * _rpc_' + str(i) + '_params;')
  cog.outl('std::lognormal_distribution<double> _rpc_' + str(i) + '_pre_dist;')
  cog.outl('std::lognormal_distribution<double> _rpc_' + str(i) + '_post_dist;')

cog.outl('public:')
cog.outl('')
cog.outl('explicit ' + serviceName +'Handler(')
for children_service in children_services:
  cog.outl('ClientPool<ThriftClient<' + children_service + 'Client>> *' + children_service + '_client_pool,')
for i in services_json[serviceName]['rpcs_send']:
  cog.outl('rpc_params * rpc_' + str(i) + '_params,') 
for i in services_json[serviceName]['rpcs_receive']:
  cog.outl('rpc_params * rpc_' + str(i) + '_params,') 
cog.outl('double scaler) {') 
for children_service in children_services:
  cog.outl('_' + children_service + '_client_pool = ' + children_service + '_client_pool;')
cog.outl('_scaler = scaler;')
cog.outl('auto seed = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
cog.outl('_gen = std::default_random_engine(seed);')

for i in services_json[serviceName]['rpcs_receive']:
  cog.outl('_rpc_' + str(i) + '_params = ' + 'rpc_' + str(i) + '_params;')
  cog.outl('double rpc_' + str(i) + '_proc_time_mean = ' + '_rpc_' + str(i) + '_params->proc_time_mean;')
  cog.outl('if(rpc_' + str(i) + '_proc_time_mean != 0) {')
  cog.outl('double rpc_' + str(i) + '_proc_time_std = ' + '_rpc_' + str(i) + '_params->proc_time_std;')
  cog.outl('double rpc_' + str(i) + '_proc_m = log(' + 'rpc_' + str(i) + '_proc_time_mean / sqrt(1 + pow(rpc_' + \
  str(i) + '_proc_time_std, 2) / pow(rpc_' + str(i) + '_proc_time_mean, 2)));')
  cog.outl('double rpc_' + str(i) + '_proc_s = sqrt(log(1 + pow(rpc_' + str(i) + '_proc_time_std, 2) /  \
  pow(rpc_' + str(i) + '_proc_time_mean, 2)));')
  cog.outl('_rpc_' + str(i) + '_proc_dist = std::lognormal_distribution<double>(rpc_' + str(i) + '_proc_m, rpc_' + \
  str(i) + '_proc_s); }')
for i in services_json[serviceName]['rpcs_send']:
  cog.outl('_rpc_' + str(i) + '_params = ' + 'rpc_' + str(i) + '_params;')
  cog.outl('double rpc_' + str(i) + '_pre_time_mean = ' + '_rpc_' + str(i) + '_params->pre_time_mean;')
  cog.outl('if(rpc_' + str(i) + '_pre_time_mean != 0) {')
  cog.outl('double rpc_' + str(i) + '_pre_time_std = ' + '_rpc_' + str(i) + '_params->pre_time_std;')
  cog.outl('double rpc_' + str(i) + '_pre_m = log(' + 'rpc_' + str(i) + '_pre_time_mean / sqrt(1 + pow(rpc_' + \
  str(i) + '_pre_time_std, 2) / pow(rpc_' + str(i) + '_pre_time_mean, 2)));')
  cog.outl('double rpc_' + str(i) + '_pre_s = sqrt(log(1 + pow(rpc_' + str(i) + '_pre_time_std, 2) /  \
  pow(rpc_' + str(i) + '_pre_time_mean, 2)));')
  cog.outl('_rpc_' + str(i) + '_pre_dist = std::lognormal_distribution<double>(rpc_' + str(i) + '_pre_m, rpc_' + \
  str(i) + '_pre_s); }')
  cog.outl('double rpc_' + str(i) + '_post_time_mean = ' + '_rpc_' + str(i) + '_params->post_time_mean;')
  cog.outl('if(rpc_' + str(i) + '_post_time_mean != 0) {')
  cog.outl('double rpc_' + str(i) + '_post_time_std = ' + '_rpc_' + str(i) + '_params->post_time_std;')
  cog.outl('double rpc_' + str(i) + '_post_m = log(' + 'rpc_' + str(i) + '_post_time_mean / sqrt(1 + pow(rpc_' + \
  str(i) + '_post_time_std, 2) / pow(rpc_' + str(i) + '_post_time_mean, 2)));')
  cog.outl('double rpc_' + str(i) + '_post_s = sqrt(log(1 + pow(rpc_' + str(i) + '_post_time_std, 2) /  \
  pow(rpc_' + str(i) + '_post_time_mean, 2)));')
  cog.outl('_rpc_' + str(i) + '_post_dist = std::lognormal_distribution<double>(rpc_' + str(i) + '_post_m, rpc_' + \
  str(i) + '_post_s); }')
cog.outl('}')

cog.outl('')
cog.outl('~' + serviceName+'Handler() override = default;')
cog.outl('')

for rpc_receive in rpcs_receive:
  rpc_name = 'rpc_' + str(rpc_receive)
  cog.outl('void ' + rpc_name + '(const std::map<std::string, std::string> & carrier) override {')
  #cog.outl('cout << "call ' + rpc_name + '!" << endl;')
  cog.outl('TextMapReader reader(carrier);')
  cog.outl('auto parent_span = opentracing::Tracer::Global()->Extract(reader);')
  cog.outl('auto span = opentracing::Tracer::Global()->StartSpan("' + rpc_name + '_server", { opentracing::ChildOf(parent_span->get()) });')
  cog.outl('')

  rpcs_send = []
  rpcs_send_dep = {}
  rpcs_num = 0
  rpcs_send_multi = {}
  rpcs_send_para = {}
  max_rpc = 0
  for edge in edges:
    if edge[0] == rpc_receive:
      rpcs_send.append(edge[1])
      if len(edge) > 2 :
        rpcs_num += edge[2]
        rpcs_send_multi[edge[1]] = edge[2]
        rpcs_send_para[edge[1]] = edge[3]
      else:
        rpcs_num += 1
  for rpc_send in rpcs_send:
    if rpc_send > max_rpc:
      max_rpc = rpc_send
    rpcs_send_dep[rpc_send] = []
    for dep in dependency:
      if dep[0] == rpc_send:
        rpcs_send_dep[rpc_send].append(dep[1])

  max_rpc += 1
  rpcs_ini_num = len(rpcs_send)
  rpc_dup_map = {}

  for i in range(rpcs_ini_num):
    if rpcs_send[i] in rpcs_send_multi:
      for j in range(rpcs_send_multi[rpcs_send[i]]-1):
        rpc_dup_map[max_rpc] = rpcs_send[i]
        rpcs_send.append(max_rpc)
        rpcs_send_dep[max_rpc] = []
        if rpcs_send_para[rpcs_send[i]] == 1: #parallel
          if rpcs_send_dep[rpcs_send[i]]:
            for k in rpcs_send_dep[rpcs_send[i]]:
              rpcs_send_dep[max_rpc].append(k)
          for kk in rpcs_send:
            if rpcs_send[i] in rpcs_send_dep[kk]:
              rpcs_send_dep[kk].append(max_rpc)
        else:
          if j == 0:
            rpcs_send_dep[max_rpc].append(rpcs_send[i])
          else:
            rpcs_send_dep[max_rpc].append(max_rpc-1)
          if j == rpcs_send_multi[rpcs_send[i]]-2:
            for kk in rpcs_send:
              if rpcs_send[i] in rpcs_send_dep[kk]:
                if (kk not in rpc_dup_map) or rpc_dup_map[kk] != rpcs_send[i]:
                  rpcs_send_dep[kk].remove(rpcs_send[i])
                  rpcs_send_dep[kk].append(max_rpc)
        max_rpc += 1
  print(rpcs_send_dep)

  rpcs_send_list = []
  for rpc_send in rpcs_send:
    insert_list(rpc_send, rpcs_send_list, rpcs_send_dep)
  print(rpcs_send_list)

  rpcs_send_map = {}
  for rpc_send in rpcs_send_list:
    rpcs_send_map[rpc_send] = rpcs_send_list.index(rpc_send)
  print(rpcs_send_map)

  rpcs_send_num = rpcs_num
  cog.outl('double proc_time;')
  cog.outl('double proc_time_mean = _rpc_' + str(rpc_receive) + '_params->proc_time_mean;')
  cog.outl('if(proc_time_mean != 0) {')
  cog.outl('DistributionType distribution_type = _rpc_' + str(rpc_receive) + '_params->distribution_type;')
  #cog.outl('double proc_time_std = _rpc_' + str(rpc_receive) + '_params->proc_time_std;')
  #cog.outl('double proc_m = log(proc_time_mean / sqrt(1 + pow(proc_time_std, 2) / pow(proc_time_mean, 2)));')
  #cog.outl('double proc_s = sqrt(log(1 + pow(proc_time_std, 2) / pow(proc_time_mean, 2)));')
  #cog.outl('_dist = std::lognormal_distribution<double>(proc_m, proc_s);')
  cog.outl('switch (distribution_type) {')
  cog.outl('case constant: proc_time = proc_time_mean;')
  cog.outl('break;')
  cog.outl('case log_normal: proc_time = _rpc_' + str(rpc_receive) + '_proc_dist(_gen);')
  cog.outl('break;')
  cog.outl('default: proc_time = proc_time_mean;')
  cog.outl('}}')
  cog.outl('else proc_time = proc_time_mean;')
  #cog.outl('cout << "proc_time: " << proc_time << endl;')
  if rpcs_send_num <= 0:
    cog.outl('proc_time *= _scaler;')
    if use_loop == 0:
      cog.outl('auto proc_t0 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
      cog.outl('int proc_loop_times = sqrt(proc_time/1000)*1000;')
      cog.outl('int proc[proc_loop_times];')
      cog.outl('int init[proc_loop_times];')
      cog.outl('for (int i = 0; i < proc_loop_times; i++) {')
      cog.outl('init[i] = rand(); }')
      cog.outl('for (int j = 0; j < 3000000; j++) {')
      cog.outl('for (int i = 0; i < proc_loop_times; i++) {')
      cog.outl('proc[i] = init[i]; }')
      cog.outl('bubble_sort(proc, proc_loop_times);}')
      cog.outl('auto proc_t1 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
      #cog.outl('cout << "proc_duration_time: " << int(proc_t1 - proc_t0) << "\tproc_time " << proc_time << endl;')
    elif use_loop == 1:
      cog.outl('auto proc_t0 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
      cog.outl('while (true) {')
      cog.outl('auto proc_t1 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
      cog.outl('if (proc_t1 - proc_t0 >= (int) (proc_time))')
      cog.outl('break;')
      cog.outl('}')
    else:
      cog.outl('std::this_thread::sleep_for(std::chrono::microseconds(int(proc_time)));')
    cog.outl('span->Finish();')
    cog.outl('}')
    continue
  else:
    #cog.outl('proc_time *= (_scaler / 2);')
    cog.outl('proc_time *= _scaler;')
    if use_loop == 0:
      cog.outl('auto proc_t0 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
      cog.outl('int proc_loop_times = sqrt(proc_time/1000)*1000;')
      cog.outl('int proc[proc_loop_times];')
      cog.outl('int init[proc_loop_times];')
      cog.outl('for (int i = 0; i < proc_loop_times; i++) {')
      cog.outl('init[i] = rand(); }')
      cog.outl('for (int j = 0; j < 3000000; j++) {')
      cog.outl('for (int i = 0; i < proc_loop_times; i++) {')
      cog.outl('proc[i] = init[i]; }')
      cog.outl('bubble_sort(proc, proc_loop_times);}')
      cog.outl('auto proc_t1 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
      #cog.outl('cout << "proc_duration_time: " << int(proc_t1 - proc_t0) << "\tproc_time " << proc_time << endl;')
    elif use_loop == 1:
      cog.outl('auto proc_t0 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
      cog.outl('while (true) {')
      cog.outl('auto proc_t1 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
      cog.outl('if (proc_t1 - proc_t0 >= (int) (proc_time))')
      cog.outl('break;')
      cog.outl('}')
    else:
      cog.outl('std::this_thread::sleep_for(std::chrono::microseconds(int(proc_time)));')

  cog.outl('vector<std::shared_future<void>> fuWaitVec[' + str(rpcs_send_num) +'];')
  cog.outl('std::shared_future<void> fuVec[' + str(rpcs_send_num) +'];')
  cog.outl('')

  for rpc_send in rpcs_send_list:
    rpc_name = "rpc_" + str(rpc_send)
    if rpc_send in rpc_dup_map:
      rpc_name = "rpc_" + str(rpc_dup_map[rpc_send])
    rpc_server_name = rpcs_json[rpc_name]['server']
    for j in rpcs_send_dep[rpc_send]:
      cog.outl('fuWaitVec[' + str(rpcs_send_map[rpc_send]) +
      '].push_back(fuVec[' + str(rpcs_send_map[j]) + ']);')

    cog.outl('fuVec[' + str(rpcs_send_map[rpc_send]) + '] = std::async(std::launch::async, [&]() {')
    cog.outl('std::map<std::string, std::string> writer_text_map;')
    cog.outl('TextMapWriter writer(writer_text_map);')

    #cog.outl('auto future_span = opentracing::Tracer::Global()->StartSpan("' + rpc_name + '_client_future", { opentracing::ChildOf(&(span->context()))});')
    #cog.outl('opentracing::Tracer::Global()->Inject(future_span->context(), writer);')

    cog.outl('if (!fuWaitVec[' + str(rpcs_send_map[rpc_send]) + '].empty()) {')
    cog.outl('for (auto &i : fuWaitVec[' + str(rpcs_send_map[rpc_send]) + ']) {')
    cog.outl('i.wait(); }')
    cog.outl('}')
    
    if(use_pre):
      cog.outl('DistributionType distribution_type = _' + rpc_name + '_params->distribution_type;')
      cog.outl('double pre_time;')
      cog.outl('double pre_time_mean = _' + rpc_name + '_params->pre_time_mean;')
      cog.outl('if(pre_time_mean != 0) {')
      #cog.outl('double pre_time_std = _' + rpc_name + '_params->pre_time_std;')
      #cog.outl('double pre_m = log(pre_time_mean / sqrt(1 + pow(pre_time_std, 2) / pow(pre_time_mean, 2)));')
      #cog.outl('double pre_s = sqrt(log(1 + pow(pre_time_std, 2) / pow(pre_time_mean, 2)));')
      #cog.outl('auto pre_dist = std::lognormal_distribution<double>(pre_m, pre_s);')
      cog.outl('switch (distribution_type) {')
      cog.outl('case constant: pre_time = pre_time_mean;')
      cog.outl('break;')
      cog.outl('case log_normal: pre_time = _' + rpc_name + '_pre_dist(_gen);')
      cog.outl('break;')
      cog.outl('default: pre_time = pre_time_mean;')
      cog.outl('}}')
      cog.outl('else pre_time = pre_time_mean;')
      cog.outl('pre_time *= _scaler;')
      #cog.outl('cout << "pre_time: " << pre_time << endl;')

      cog.outl('double post_time;')
      cog.outl('double post_time_mean = _' + rpc_name + '_params->post_time_mean;')
      cog.outl('if(post_time_mean != 0) {')
      #cog.outl('double post_time_std = _' + rpc_name + '_params->post_time_std;')
      #cog.outl('double post_m = log(post_time_mean / sqrt(1 + pow(post_time_std, 2) / pow(post_time_mean, 2)));')
      #cog.outl('double post_s = sqrt(log(1 + pow(post_time_std, 2) / pow(post_time_mean, 2)));')
      #cog.outl('auto post_dist = std::lognormal_distribution<double>(post_m, post_s);')
      cog.outl('switch (distribution_type) {')
      cog.outl('case constant: post_time = post_time_mean;')
      cog.outl('break;')
      cog.outl('case log_normal: post_time = _' + rpc_name + '_post_dist(_gen);')
      cog.outl('break;')
      cog.outl('default: post_time = post_time_mean;')
      cog.outl('}}')
      cog.outl('else post_time = post_time_mean;')
      cog.outl('post_time *= _scaler;')
      
      if use_loop == 0:
        cog.outl('auto pre_t0 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
        cog.outl('int pre_loop_times = sqrt(pre_time/1000)*1000;')
        cog.outl('int pre[pre_loop_times];')
        cog.outl('int init[pre_loop_times];')
        cog.outl('for (int i = 0; i < pre_loop_times; i++) {')
        cog.outl('init[i] = rand(); }')
        cog.outl('for (int j = 0; j < 3000000; j++) {')
        cog.outl('for (int i = 0; i < pre_loop_times; i++) {')
        cog.outl('pre[i] = init[i]; }')
        cog.outl('bubble_sort(pre, pre_loop_times);}')
        cog.outl('auto pre_t1 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
        #cog.outl('cout << "pre_duration_time: " << int(pre_t1 - pre_t0) << "\tpre_time " << pre_time << endl;')
      elif use_loop == 1:
        cog.outl('auto pre_t0 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
        cog.outl('while (true) {')
        cog.outl('auto pre_t1 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
        cog.outl('if (pre_t1 - pre_t0 >= (int) (pre_time))')
        cog.outl('break;')
        cog.outl('}')
      else:
        cog.outl('std::this_thread::sleep_for(std::chrono::microseconds(int(pre_time)));')

    cog.outl('auto self_span = opentracing::Tracer::Global()->StartSpan("' + rpc_name + '_client", { opentracing::ChildOf(&(span->context()))});')
    cog.outl('opentracing::Tracer::Global()->Inject(self_span->context(), writer);')

    client_wrapper = rpc_server_name + '_client_wrapper'
    cog.outl('auto ' + rpc_server_name + '_client_wrapper = _' + rpc_server_name + '_client_pool->Pop();')
    #cog.outl('cout << "pool size: " << _' + rpc_server_name + '_client_pool->getPoolSize() << endl;')
    cog.outl('if(!' + client_wrapper + ') {')
    cog.outl('ServiceException se;')
    cog.outl('se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;')
    cog.outl('se.message = "Failed to connect to ' + rpc_server_name + '";')
    cog.outl('throw se;')
    cog.outl('}')
    cog.outl('else {')
    cog.outl('auto ' + rpc_server_name + '_client = ' + client_wrapper + '->GetClient();')
    cog.outl('try {')
    cog.outl(rpc_server_name + '_client->' + rpc_name + '(writer_text_map);')
    cog.outl('} catch (TException& tx) {')
    cog.outl('cout << "ERROR: " << tx.what() << endl;')
    cog.outl('_' + rpc_server_name + '_client_pool->Push(' + client_wrapper + ');')
    cog.outl('}')
    cog.outl('_' + rpc_server_name + '_client_pool->Push(' + client_wrapper + ');')
    cog.outl('}')
    cog.outl('self_span->Finish();')
    if(use_pre):
      if use_loop == 0:
        cog.outl('auto post_t0 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
        cog.outl('int post_loop_times = sqrt(post_time/1000)*1000;')
        cog.outl('int post[post_loop_times];')
        cog.outl('int init[post_loop_times];')
        cog.outl('for (int i = 0; i < post_loop_times; i++) {')
        cog.outl('init[i] = rand(); }')
        cog.outl('for (int j = 0; j < 3000000; j++) {')
        cog.outl('for (int i = 0; i < post_loop_times; i++) {')
        cog.outl('post[i] = init[i]; }')
        cog.outl('bubble_sort(post, post_loop_times);}')
        cog.outl('auto post_t1 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
        #cog.outl('cout << "post_duration_time: " << int(post_t1 - pre_t0) << "\tpost_time " << post_time << endl;')
      elif use_loop == 1:
        cog.outl('auto post_t0 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
        cog.outl('while (true) {')
        cog.outl('auto post_t1 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
        cog.outl('if (post_t1 - post_t0 >= (int) (post_time))')
        cog.outl('break;')
        cog.outl('}')
      else:
        cog.outl('std::this_thread::sleep_for(std::chrono::microseconds(int(post_time)));')

    cog.outl('});')
    cog.outl('')

  cog.outl('for (auto &i : fuVec) {')
  cog.outl('i.wait();')
  cog.outl('}')
  
  '''
  if use_loop == 0:
    cog.outl('proc_t0 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
    cog.outl('for (int i = 0; i < proc_loop_times; i++) {')
    cog.outl('init[i] = rand(); }')
    cog.outl('for (int j = 0; j < 3000000; j++) {')
    cog.outl('for (int i = 0; i < proc_loop_times; i++) {')
    cog.outl('proc[i] = init[i]; }')
    cog.outl('bubble_sort(proc, proc_loop_times);}')
    cog.outl('proc_t1 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
    #cog.outl('cout << "proc_duration_time: " << int(proc_t1 - proc_t0) << "\tproc_time " << proc_time << endl;')
  elif use_loop == 1:
    cog.outl('proc_t0 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
    cog.outl('while (true) {')
    cog.outl('auto proc_t1 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
    cog.outl('if (proc_t1 - proc_t0 >= (int) (proc_time))')
    cog.outl('break;')
    cog.outl('}')
  else:
    cog.outl('std::this_thread::sleep_for(std::chrono::microseconds(int(proc_time)));')
  '''
  
  cog.outl('span->Finish();')
  cog.outl('}')

cog.outl('};')

]]]*/
//[[[end]]]

void startServer(TServer &server) {
  /*[[[cog
      import cog
      cog.outl('cout << "Starting the '+ serviceName + ' server..." << endl;')
      ]]]*/
  //[[[end]]]
  server.serve();
  cout << "Done." << endl;
}

int main(int argc, char *argv[]) {
  json rpcs_json;
  json services_json;
  std::ifstream json_file;
  json_file.open("config/rpcs.json");
  if (json_file.is_open()) {
    json_file >> rpcs_json;
    json_file.close();
  }
  else {
    cout << "Cannot open rpcs-config.json" << endl;
    return -1;
  }
  json_file.open("config/services.json");
  if (json_file.is_open()) {
    json_file >> services_json;
    json_file.close();
  }
  else {
    cout << "Cannot open services-config.json" << endl;
    return -1;
  }

  /*[[[cog
  import cog
  cog.outl('srand((unsigned)time(NULL));')
  cog.outl('SetUpTracer("config/jaeger-config.yml", "' + serviceName + '");')
  for children_service in children_services:
    cog.outl('std::string ' + children_service + '_addr = services_json["' + children_service + '"]["server_addr"];')
    cog.outl('int ' + children_service + '_port = services_json["' + children_service + '"]["server_port"];')
    cog.outl('ClientPool<ThriftClient<' + children_service + 'Client>> ' + children_service + '_client_pool(')
    cog.outl('"' + children_service + '", ' + children_service + '_addr, ' + children_service + '_port, ' + str(0) + ', ' + str(512) + ', ' + str(5000) + ');')
    cog.outl('')
  
  for i in services_json[serviceName]['rpcs_send']:
    cog.outl('string tmp_rpc_' + str(i) + '_distribution_type = rpcs_json["rpc_' + str(i) + '"]["distribution_type"];')
    cog.outl('DistributionType rpc_' + str(i) + '_distribution_type;')
    cog.outl('if (tmp_rpc_' + str(i) + '_distribution_type == "log_normal")')
    cog.outl('rpc_' + str(i) + '_distribution_type = log_normal;')
    cog.outl('else')
    cog.outl('rpc_' + str(i) + '_distribution_type = constant;')
    cog.outl('rpc_params rpc_' + str(i) + '_params = {')
    cog.outl('rpc_' + str(i) + '_distribution_type,')
    cog.outl('rpcs_json["rpc_' + str(i) + '"]["pre_time_mean"],')
    cog.outl('rpcs_json["rpc_' + str(i) + '"]["pre_time_std"],')
    cog.outl('rpcs_json["rpc_' + str(i) + '"]["post_time_mean"],')
    cog.outl('rpcs_json["rpc_' + str(i) + '"]["post_time_std"],')
    cog.outl('rpcs_json["rpc_' + str(i) + '"]["proc_time_mean"],')
    cog.outl('rpcs_json["rpc_' + str(i) + '"]["proc_time_std"],')
    cog.outl('};')
    cog.outl('')

  for i in services_json[serviceName]['rpcs_receive']:
    cog.outl('string tmp_rpc_' + str(i) + '_distribution_type = rpcs_json["rpc_' + str(i) + '"]["distribution_type"];')
    cog.outl('DistributionType rpc_' + str(i) + '_distribution_type;')
    cog.outl('if (tmp_rpc_' + str(i) + '_distribution_type == "log_normal")')
    cog.outl('rpc_' + str(i) + '_distribution_type = log_normal;')
    cog.outl('else')
    cog.outl('rpc_' + str(i) + '_distribution_type = constant;')
    cog.outl('rpc_params rpc_' + str(i) + '_params = {')
    cog.outl('rpc_' + str(i) + '_distribution_type,')
    cog.outl('rpcs_json["rpc_' + str(i) + '"]["pre_time_mean"],')
    cog.outl('rpcs_json["rpc_' + str(i) + '"]["pre_time_std"],')
    cog.outl('rpcs_json["rpc_' + str(i) + '"]["post_time_mean"],')
    cog.outl('rpcs_json["rpc_' + str(i) + '"]["post_time_std"],')
    cog.outl('rpcs_json["rpc_' + str(i) + '"]["proc_time_mean"],')
    cog.outl('rpcs_json["rpc_' + str(i) + '"]["proc_time_std"],')
    cog.outl('};')
    cog.outl('')

  cog.outl('double scaler = services_json["' + serviceName + '"]["scaler"];')
  cog.outl('int port = services_json["' + serviceName + '"]["server_port"];')

  class ServerType(Enum):
    TThreaded = 1
    TThreadPool = 2
    TNonblocking = 3

  serverType = ServerType.TThreaded
  cog.outl('int poolSize = 50;')

  if serverType == ServerType.TThreaded:
    cog.outl('TThreadedServer server (')
    cog.outl('stdcxx::make_shared<' + serviceName + 'Processor>(')
    cog.outl('stdcxx::make_shared<' + serviceName + 'Handler>(')
    for i in children_services:
      cog.outl('&' + i + '_client_pool,')
    for i in services_json[serviceName]['rpcs_send']:
      cog.outl('&rpc_' + str(i) + '_params,')
    for i in services_json[serviceName]['rpcs_receive']:
      cog.outl('&rpc_' + str(i) + '_params,')
    cog.outl(' scaler)),')
    cog.outl('stdcxx::make_shared<TServerSocket>(' + '"0.0.0.0", port' + '),')
    cog.outl('stdcxx::make_shared<TFramedTransportFactory>(),')
    cog.outl('stdcxx::make_shared<TBinaryProtocolFactory>());')
  elif serverType == ServerType.TNonblocking:
    cog.outl('auto trans = make_shared<TNonblockingServerSocket>(port);')
    cog.outl('auto processor = make_shared<' + serviceName + 'Processor>(make_shared<' + serviceName + 'Handler>(')
    for i in children_services:
      cog.outl('&' + i + '_client_pool,')
    for i in services_json[serviceName]['rpcs_send']:
      cog.outl('&rpc_' + str(i) + '_params,')
    for i in services_json[serviceName]['rpcs_receive']:
      cog.outl('&rpc_' + str(i) + '_params,')
    cog.outl(' scaler));')
    cog.outl('auto protocol = make_shared<TBinaryProtocolFactory>();')
    cog.outl('auto thread_manager = ThreadManager::newSimpleThreadManager(poolSize);')
    cog.outl('thread_manager->threadFactory(make_shared<PosixThreadFactory>());')
    cog.outl('thread_manager->start();')
    cog.outl('TNonblockingServer server(processor, protocol, trans, thread_manager);')
  elif serverType == ServerType.TThreadPool:
    cog.outl('auto thread_manager = ThreadManager::newSimpleThreadManager(poolSize);')
    cog.outl('thread_manager->threadFactory(make_shared<PosixThreadFactory>());')
    cog.outl('thread_manager->start();')
    cog.outl('TThreadPoolServer server (')
    cog.outl('stdcxx::make_shared<' + serviceName + 'Processor>(')
    cog.outl('stdcxx::make_shared<' + serviceName + 'Handler>(')
    for i in children_services:
      cog.outl('&' + i + '_client_pool,')
    for i in services_json[serviceName]['rpcs_send']:
      cog.outl('&rpc_' + str(i) + '_params,')
    for i in services_json[serviceName]['rpcs_receive']:
      cog.outl('&rpc_' + str(i) + '_params,')
    cog.outl(' scaler)),')
    cog.outl('stdcxx::make_shared<TServerSocket>(' + '"0.0.0.0", port' + '),')
    cog.outl('stdcxx::make_shared<TFramedTransportFactory>(),')
    cog.outl('stdcxx::make_shared<TBinaryProtocolFactory>(),')
    cog.outl('thread_manager);')
  else: #default TThreadedServer
    cog.outl('TThreadedServer server (')
    cog.outl('stdcxx::make_shared<' + serviceName + 'Processor>(')
    cog.outl('stdcxx::make_shared<' + serviceName + 'Handler>(')
    for i in children_services:
      cog.outl('&' + i + '_client_pool,')
    for i in services_json[serviceName]['rpcs_send']:
      cog.outl('&rpc_' + str(i) + '_params,')
    for i in services_json[serviceName]['rpcs_receive']:
      cog.outl('&rpc_' + str(i) + '_params,')
    cog.outl(' scaler)),')
    cog.outl('stdcxx::make_shared<TServerSocket>(' + '"0.0.0.0", port' + '),')
    cog.outl('stdcxx::make_shared<TFramedTransportFactory>(),')
    cog.outl('stdcxx::make_shared<TBinaryProtocolFactory>());')

  ]]]*/
  //[[[end]]]
  startServer(server);
}
