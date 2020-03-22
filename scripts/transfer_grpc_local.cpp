#include <iostream>
#include <memory>
#include <string>
#include "../tracing.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <time.h>
#include <mutex>

#include <grpcpp/grpcpp.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

using namespace grpc;
using namespace helloworld;
using namespace std;
using json = nlohmann::json;
using std::chrono::microseconds;
using std::chrono::duration_cast;
using std::chrono::system_clock;

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

long getCurrentTime() { 
  struct timeval tv; 
  gettimeofday(&tv,NULL); 
  return tv.tv_sec * 1000 + tv.tv_usec/1000; 
} 

/*[[[cog
import cog
import json
import math
from enum import Enum

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

rpcs_receive_send = {} # map: rpc_receive to rpcs sent (id, times) by this rpc
for rpc_receive in services_json[serviceName]['rpcs_receive']:
  rpcs_receive_send[rpc_receive] = []
  for edge in edges:
    if edge[0] == rpc_receive:
      temp_list = [] # temp_list[0] = rpc_send_id, temp_list[1] = send times
      temp_list.append(edge[1])
      if len(edge) > 2:
        temp_list.append(edge[2])
      else:
        temp_list.append(1)
      rpcs_receive_send[rpc_receive].append(temp_list)

rpcs_receive_dic = {}

for rpc_receive in services_json[serviceName]['rpcs_receive']:
  rpcs_receive_dic[rpc_receive] = {}

  rpcs_send = [] # list of rpcs to send, not real rpc_id, if replica id equals rpc_replica_map[rpc_send]
  max_rpc = 0 # maximum of rpc id, avoid overlap when handling replica(call multiple times)
  rpcs_send_multi = {} # map: rpc_send to its send times if multiple
  rpcs_send_para = {} # map: rpc_send to its inside dependicies (1 for parallel 0 for sequential) if multiple
  rpcs_send_dep = {} # map: rpc_send to its dependencies (in rpcs_sent, not real id)
  for edge in edges:
    if edge[0] == rpc_receive:
      rpcs_send.append(edge[1])
      if len(edge) > 2:
        rpcs_send_multi[edge[1]] = edge[2]
        rpcs_send_para[edge[1]] = edge[3]
  for rpc_send in rpcs_send:
    if rpc_send > max_rpc:
      max_rpc = rpc_send
    rpcs_send_dep[rpc_send] = []
    for dep in dependency:
      if dep[0] == rpc_send:
        rpcs_send_dep[rpc_send].append(dep[1])

  rpcs_ini_num = len(rpcs_send)
  rpcs_current_num = max_rpc + 1
  rpc_replica_map = {} # map: rpc replica to its real rpc_id if multiple

  for i in range(rpcs_ini_num):
    if rpcs_send[i] in rpcs_send_multi:
      for j in range(rpcs_send_multi[rpcs_send[i]] - 1):
        rpc_replica_map[rpcs_current_num] = rpcs_send[i]
        rpcs_send.append(rpcs_current_num)
        rpcs_send_dep[rpcs_current_num] = []
        # parallel, add each replica to dependency of downstream rpcs, add upstream rpcs to dependency of each replica 
        if rpcs_send_para[rpcs_send[i]] == 1:
          if rpcs_send_dep[rpcs_send[i]]:
            for k in rpcs_send_dep[rpcs_send[i]]:
              rpcs_send_dep[rpcs_current_num].append(k)
          for k in rpcs_send:
            if rpcs_send[i] in rpcs_send_dep[k]:
              rpcs_send_dep[k].append(rpcs_current_num)
        # sequential, replace the original one to last replica in dependency of downstream rpcs,
        # add each replica to dependency of its following one
        else:
          if j == 0:
            rpcs_send_dep[rpcs_current_num].append(rpcs_send[i])
          else:
            rpcs_send_dep[rpcs_current_num].append(rpcs_current_num - 1)
          if j == rpcs_send_multi[rpcs_send[i]] - 2:
            for k in rpcs_send:
              if rpcs_send[i] in rpcs_send_dep[k]:
                if (k not in rpc_replica_map) or rpc_replica_map[k] != rpcs_send[i]:
                  rpcs_send_dep[k].remove(rpcs_send[i])
                  rpcs_send_dep[k].append(rpcs_current_num)
        rpcs_current_num += 1

  rpcs_receive_dic[rpc_receive]["rpcs_send"] = rpcs_send
  rpcs_receive_dic[rpc_receive]["rpcs_send_dep"] = rpcs_send_dep
  rpcs_receive_dic[rpc_receive]["rpc_replica_map"] = rpc_replica_map

  # print(rpc_receive, rpcs_receive_dic[rpc_receive])

]]]*/
//[[[end]]]


/*[[[cog
import cog

cog.outl('class CommonCallData {')
cog.outl('public:')
cog.outl(serviceName + '::AsyncService* service_;')
cog.outl('ServerCompletionQueue* cq_;')
cog.outl('ServerContext ctx_;')
cog.outl('HelloRequest request_;')
cog.outl('HelloReply reply_;')
cog.outl('ServerAsyncResponseWriter<HelloReply> responder_;')
cog.outl('enum CallStatus { CREATE, PROCESS, FINISH, DESTROY };')
cog.outl('CallStatus status_;')
cog.outl('std::unique_ptr<opentracing::Span> span;')
cog.outl('explicit CommonCallData(' + serviceName + '::AsyncService* service, ServerCompletionQueue* cq)')
cog.outl(':service_(service), cq_(cq), responder_(&ctx_), status_(CREATE) {}')
cog.outl('virtual ~CommonCallData() {}')
cog.outl('virtual void Proceed(int ID = -1) = 0;')
cog.outl('};')
cog.outl('')

cog.outl('class AbstractAsyncClientCall {')
cog.outl('public:')
cog.outl(serviceName + '::AsyncService* service_;')
cog.outl('Status status;')
cog.outl('ClientContext context;')
cog.outl('HelloRequest request;')
cog.outl('HelloReply reply;')
cog.outl('virtual ~AbstractAsyncClientCall() {}')
cog.outl('virtual void Proceed(bool = true) = 0;')
cog.outl('};')
cog.outl('')

for i in services_json[serviceName]['rpcs_send']:
  rpc_name = 'Rpc_' + str(i)
  cog.outl('class Async' + rpc_name + '_ClientCall: public AbstractAsyncClientCall {')
  cog.outl('public:')
  cog.outl('std::unique_ptr<ClientAsyncResponseReader<HelloReply>> responder;')
  cog.outl('std::unique_ptr<opentracing::Span> span;')
  cog.outl('CommonCallData* call_;')
  cog.outl('int requestID;')
  cog.outl('')
  cog.outl('Async' + rpc_name + '_ClientCall(CompletionQueue& cq_, std::unique_ptr<')
  cog.outl(rpcs_json['rpc_' + str(i)]['server'] + '::Stub>& stub_,')
  cog.outl('CommonCallData* call, int ID):AbstractAsyncClientCall(), call_(call), requestID(ID) {')
  cog.outl('std::map<std::string, std::string> writer_text_map;')
  cog.outl('TextMapWriter writer(writer_text_map);')
  cog.outl('span = opentracing::Tracer::Global()->StartSpan("rpc_' + str(i) + '_client", {opentracing::ChildOf(&((call_->span)->context()))});')
  cog.outl('opentracing::Tracer::Global()->Inject(span->context(), writer);')
  cog.outl('request.set_name(writer_text_map.begin()->second);')
  cog.outl('responder = stub_->Asyncrpc_' + str(i) + '(&context, request, &cq_);')
  cog.outl('responder->Finish(&reply, &status, (void*)this); }')
  cog.outl('')
  cog.outl('virtual void Proceed(bool ok = true) override {')
  cog.outl('span->Finish();')
  cog.outl('GPR_ASSERT(ok);')
  cog.outl('if (!status.ok()) {')
  cog.outl('cout << "' + serviceName + ' forward rpc_' + str(i) + ' fail! Error code: ";')
  cog.outl('cout << status.error_code() << ": " << status.error_message() << endl; }')
  cog.outl('call_->Proceed(requestID);')
  cog.outl('delete this; }')
  cog.outl('};')
  cog.outl('')

  cog.outl('class ' + rpc_name + '_Client {')
  cog.outl('private:')
  cog.outl('std::unique_ptr<' + rpcs_json['rpc_' + str(i)]['server'] + '::Stub> stub_;')
  cog.outl('CompletionQueue cq_;')
  cog.outl('public:')
  cog.outl('explicit ' + rpc_name + '_Client(std::shared_ptr<Channel> channel)')
  cog.outl(': stub_(' + rpcs_json['rpc_' + str(i)]['server'] + '::NewStub(channel)) {}')
  cog.outl('')
  cog.outl('void Forward(CommonCallData* call, int ID) {')
  cog.outl('Async' + rpc_name + '_ClientCall* call_ = new Async' + rpc_name + '_ClientCall(cq_, stub_, call, ID);}')
  cog.outl('')
  cog.outl('void AsyncCompleteRpc() {')
  cog.outl('void *got_tag;')
  cog.outl('bool ok = false;')
  cog.outl('while (cq_.Next(&got_tag, &ok)) {')
  cog.outl('AbstractAsyncClientCall *call = static_cast<AbstractAsyncClientCall *>(got_tag);')
  cog.outl('call->Proceed(ok); }')
  cog.outl('cout << "' + rpc_name + '_Client Completion queue is shutting down." << endl;')
  cog.outl('}')
  cog.outl('};')
  cog.outl('')
  
for i in services_json[serviceName]['rpcs_receive']:

  rpcs_send_list = rpcs_receive_dic[i]["rpcs_send"]
  rpcs_send_dep = rpcs_receive_dic[i]["rpcs_send_dep"]
  rpc_replica_map = rpcs_receive_dic[i]["rpc_replica_map"]
  
  cog.outl('class Rpc_' + str(i) + '_CallData: public CommonCallData {')
  cog.outl('public:')
  cog.outl('std::lognormal_distribution<double>* rpc_' + str(i) + '_proc_dist_;')
  cog.outl('std::default_random_engine gen;')
  for j in range(len(rpcs_receive_send[i])):
    cog.outl('Rpc_' + str(rpcs_receive_send[i][j][0]) + '_Client* rpc_' + str(rpcs_receive_send[i][j][0]) + '_client_;')
  if len(rpcs_send_list) > 0:
    cog.outl('bool sent[' + str(len(rpcs_send_list)) + '] = { false };')
    cog.outl('bool getResponse[' + str(len(rpcs_send_list)) + '] = { false };')
    cog.outl('std::mutex _mtx;')


  cog.outl('Rpc_' + str(i) + '_CallData(' + serviceName + '::AsyncService* service, ServerCompletionQueue* cq')
  cog.outl(', std::lognormal_distribution<double>* rpc_' + str(i) + '_proc_dist')
  if len(rpcs_receive_send[i]) > 0:
    for j in range(len(rpcs_receive_send[i])):
      if j == len(rpcs_receive_send[i]) - 1:
        cog.outl(', Rpc_' + str(rpcs_receive_send[i][j][0]) + '_Client* rpc_' + str(rpcs_receive_send[i][j][0]) + '_client): ')
      else:
        cog.outl(', Rpc_' + str(rpcs_receive_send[i][j][0]) + '_Client* rpc_' + str(rpcs_receive_send[i][j][0]) + '_client')
  else:
    cog.outl('):')

  cog.outl('CommonCallData(service, cq)')
  cog.outl(', rpc_' + str(i) + '_proc_dist_(rpc_' + str(i) + '_proc_dist)')
  if len(rpcs_receive_send[i]) > 0:
    for j in range(len(rpcs_receive_send[i])):
      if j == len(rpcs_receive_send[i]) - 1:
        cog.outl(', rpc_' + str(rpcs_receive_send[i][j][0]) + '_client_(rpc_' + str(rpcs_receive_send[i][j][0]) + '_client) {')
      else:
        cog.outl(', rpc_' + str(rpcs_receive_send[i][j][0]) + '_client_(rpc_' + str(rpcs_receive_send[i][j][0]) + '_client)')
  else:
    cog.outl(' {')
  cog.outl('Proceed(); }')
  cog.outl('')

  cog.outl('virtual void Proceed(int ID = -1) override {')
  cog.outl('if (status_ == CREATE) {')
  cog.outl('status_ = PROCESS;')
  cog.outl('service_->Requestrpc_' + str(i) + '(&ctx_, &request_, &responder_, cq_, cq_, this);')
  cog.outl('auto seed = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
  cog.outl('gen = std::default_random_engine(seed);')
  cog.outl('}')

  cog.outl('else if (status_ == PROCESS) {')
  cog.outl('new Rpc_' + str(i) + '_CallData(service_, cq_')
  cog.outl(', rpc_' + str(i) + '_proc_dist_')
  if len(rpcs_receive_send[i]) > 0:
    for j in range(len(rpcs_receive_send[i])):
      if j == len(rpcs_receive_send[i]) - 1:
        cog.outl(', rpc_' + str(rpcs_receive_send[i][j][0]) + '_client_);')
      else:
        cog.outl(', rpc_' + str(rpcs_receive_send[i][j][0]) + '_client_')
    cog.outl('')
  else:
    cog.outl(');')
    cog.outl('')

  cog.outl('std::map<std::string, std::string> client_context_map;')
  cog.outl('client_context_map.insert(pair<string,string>("uber-trace-id", request_.name()));')
  cog.outl('TextMapReader reader(client_context_map);')
  cog.outl('auto parent_span = opentracing::Tracer::Global()->Extract(reader);')
  cog.outl('span = opentracing::Tracer::Global()->StartSpan("rpc_' + str(i) + '_server", {')
  cog.outl('opentracing::ChildOf(parent_span->get())});')
  cog.outl('')

  cog.outl('double proc_time = (*rpc_' + str(i) + '_proc_dist_)(gen);')
  #cog.outl('cout << "proc time " << proc_time << endl;')
  cog.outl('auto proc_t0 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
  cog.outl('while (true) {')
  cog.outl('auto proc_t1 = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();')
  cog.outl('if (proc_t1 - proc_t0 >= (int) (proc_time))')
  cog.outl('break;')
  cog.outl('}')
  cog.outl('')


  for rpc_send in rpcs_send_list:
    if len(rpcs_send_dep[rpc_send]) == 0:
      if rpc_send in rpc_replica_map:
        cog.outl('rpc_' + str(rpc_replica_map[rpc_send]) + '_client_->Forward(this,' + \
        str(rpcs_send_list.index(rpc_send)) + ');')
      else:
        cog.outl('rpc_' + str(rpc_send) + '_client_->Forward(this,' + \
        str(rpcs_send_list.index(rpc_send)) + ');')
      cog.outl('sent[' + str(rpcs_send_list.index(rpc_send)) + '] = true;')
  cog.outl('')

  if len(rpcs_receive_send[i]) > 0:
    cog.outl('status_ = FINISH; }')
  else:
    cog.outl('span->Finish();')
    cog.outl('responder_.Finish(reply_, Status::OK, this);')
    cog.outl('status_ = DESTROY; }')

  cog.outl('else if (status_ == FINISH) {')
  if len(rpcs_receive_send[i]) > 0:
    cog.outl('std::unique_lock<std::mutex> cv_lock(_mtx);')
    cog.outl('try {')
    cog.outl('getResponse[ID] = true;')
    for rpc_send in rpcs_send_list:
      if len(rpcs_send_dep[rpc_send]) != 0:
        cog.outl('if(!sent[' + str(rpcs_send_list.index(rpc_send)) + '] && ')
        for j in range(len(rpcs_send_dep[rpc_send])):
          if j == len(rpcs_send_dep[rpc_send]) - 1:
            cog.outl('getResponse[' + str(rpcs_send_list.index(rpcs_send_dep[rpc_send][j])) + ']) {')
          else:
            cog.outl('getResponse[' + str(rpcs_send_list.index(rpcs_send_dep[rpc_send][j])) + '] &&')
        if rpc_send in rpc_replica_map:
          cog.outl('rpc_' + str(rpc_replica_map[rpc_send]) + '_client_->Forward(this,' + \
          str(rpcs_send_list.index(rpc_send)) + ');')
        else:
          cog.outl('rpc_' + str(rpc_send) + '_client_->Forward(this,' + \
          str(rpcs_send_list.index(rpc_send)) + ');')
        cog.outl('sent[' + str(rpcs_send_list.index(rpc_send)) + '] = true;')
        cog.outl('}')
      cog.outl('')
    
    cog.outl('if (')
    for j in range(len(rpcs_send_list) - 1):
      cog.outl('getResponse[' + str(j) + '] && ' )
    cog.outl('getResponse[' + str(len(rpcs_send_list) - 1) + ']) {' )
  cog.outl('span->Finish();')
  cog.outl('responder_.Finish(reply_, Status::OK, this);')
  cog.outl('status_ = DESTROY; }')
  if len(rpcs_receive_send[i]) > 0:
    cog.outl('} catch (...) { cv_lock.unlock(); return; }')
    cog.outl('cv_lock.unlock();')
    cog.outl('}')
  cog.outl('else {')
  cog.outl('GPR_ASSERT(status_ == DESTROY);')
  cog.outl('delete this; }')
  cog.outl('}')
  cog.outl('};')
  cog.outl('')


cog.outl('class ServerImpl final {')
cog.outl('public:')
cog.outl('std::unique_ptr<ServerCompletionQueue> cq_;')
cog.outl('std::unique_ptr<Server> server_;')
cog.outl(serviceName + '::AsyncService service_;')

cog.outl('~ServerImpl() {')
cog.outl('server_->Shutdown();')
cog.outl('cq_->Shutdown(); }')
cog.outl('')
]]]*/
//[[[end]]]

  void Run() {
    json rpcs_json;
    json services_json;
    std::ifstream json_file;
    json_file.open("/home/ml2585/auto_grpc/config/rpcs.json");
    if (json_file.is_open()) {
      json_file >> rpcs_json;
      json_file.close();
    }
    else {
      cout << "Cannot open rpcs_config.json" << endl;
      return;
    }
    json_file.open("/home/ml2585/auto_grpc/config/services.json");
    if (json_file.is_open()) {
      json_file >> services_json;
      json_file.close();
    }
    else {
      cout << "Cannot open services_config.json" << endl;
      return;
    }

    /*[[[cog
    import cog

    cog.outl('int port = services_json["' + serviceName + '"]["server_port"];')
    cog.outl('string server_address("0.0.0.0:" + to_string(port));')
    cog.outl('ServerBuilder builder;')
    cog.outl('builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());')
    cog.outl('builder.RegisterService(&service_);')
    cog.outl('cq_ = builder.AddCompletionQueue();')
    cog.outl('server_ = builder.BuildAndStart();')
    cog.outl('std::cout << "Server listening on " << server_address << std::endl;')
    cog.outl('')
    
    for children_service in children_services:
      cog.outl('std::string ' + children_service + '_addr = services_json["' + children_service + '"]["server_addr"];')
      cog.outl('int ' + children_service + '_port = services_json["' + children_service + '"]["server_port"];')
    cog.outl('')

    for i in services_json[serviceName]['rpcs_receive']:
      cog.outl('// currently distribution type not use, if const set std = 0')
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
      cog.outl('std::lognormal_distribution<double> rpc_' + str(i) + '_proc_dist;')
      cog.outl('double rpc_' + str(i) + '_proc_time_mean = rpc_' + str(i) + '_params.proc_time_mean;')
      cog.outl('if(rpc_' + str(i) + '_proc_time_mean != 0) {')
      cog.outl('double rpc_' + str(i) + '_proc_time_std = rpc_' + str(i) + '_params.proc_time_std;')
      cog.outl('double rpc_' + str(i) + '_proc_m = log(rpc_' + str(i) + '_proc_time_mean / sqrt(1 + pow(rpc_' + \
      str(i) + '_proc_time_std, 2) / pow(rpc_' + str(i) + '_proc_time_mean, 2)));')
      cog.outl('double rpc_' + str(i) + '_proc_s = sqrt(log(1 + pow(rpc_' + str(i) + '_proc_time_std, 2) /  \
      pow(rpc_' + str(i) + '_proc_time_mean, 2)));')
      cog.outl('rpc_' + str(i) + '_proc_dist = std::lognormal_distribution<double>(rpc_' + str(i) + '_proc_m, rpc_' + \
      str(i) + '_proc_s); }')
      cog.outl('')

    for i in services_json[serviceName]['rpcs_send']:
      cog.outl('Rpc_' + str(i) + '_Client rpc_' + str(i) + '_client(grpc::CreateChannel(')
      cog.outl(rpcs_json['rpc_' + str(i)]['server'] + '_addr + ":" + to_string(' + rpcs_json['rpc_' + str(i)]['server'] + '_port), ')
      cog.outl('grpc::InsecureChannelCredentials()));')
      cog.outl('thread rpc_' + str(i) + '_thread = thread(&Rpc_' + str(i) + '_Client::AsyncCompleteRpc, &rpc_' + str(i) + '_client);')
    cog.outl('')
    
    for i in services_json[serviceName]['rpcs_receive']:
      cog.outl('new Rpc_' + str(i) + '_CallData(&service_, cq_.get()')
      cog.outl(', &rpc_' + str(i) + '_proc_dist')
      if len(rpcs_receive_send[i]) > 0:
        for j in range(len(rpcs_receive_send[i])):
          if j == len(rpcs_receive_send[i]) - 1:
            cog.outl(', &rpc_' + str(rpcs_receive_send[i][j][0]) + '_client);')
          else:
            cog.outl(', &rpc_' + str(rpcs_receive_send[i][j][0]) + '_client')
      else:
        cog.outl(');')
    cog.outl('')

    cog.outl('void* tag;')
    cog.outl('bool ok;')
    cog.outl('while (true) {')
    cog.outl('GPR_ASSERT(cq_->Next(&tag, &ok));')
    cog.outl('GPR_ASSERT(ok);')
    cog.outl('std::thread([=] {')
    cog.outl('static_cast<CommonCallData*>(tag)->Proceed();')
    cog.outl('}).detach();')
    cog.outl('}')

    for i in services_json[serviceName]['rpcs_send']:
      cog.outl('rpc_' + str(i) + '_thread.join();')

    cog.outl('}')
    cog.outl('};')

    cog.outl('')
    cog.outl('int main(int argc, char** argv) {')
    cog.outl('SetUpTracer("/home/ml2585/auto_grpc/config/jaeger.yml", "' + serviceName + '");')
    cog.outl('ServerImpl server;')
    cog.outl('server.Run();')
    cog.outl('return 0; }')

    ]]]*/
    //[[[end]]]
