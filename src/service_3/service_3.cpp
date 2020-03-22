#include <time.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include "../tracing.h"

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

enum DistributionType { constant, log_normal };

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
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

class CommonCallData {
 public:
  service_3::AsyncService* service_;
  ServerCompletionQueue* cq_;
  ServerContext ctx_;
  HelloRequest request_;
  HelloReply reply_;
  ServerAsyncResponseWriter<HelloReply> responder_;
  enum CallStatus { CREATE, PROCESS, FINISH, DESTROY };
  CallStatus status_;
  std::unique_ptr<opentracing::Span> span;
  explicit CommonCallData(service_3::AsyncService* service,
                          ServerCompletionQueue* cq)
      : service_(service), cq_(cq), responder_(&ctx_), status_(CREATE) {}
  virtual ~CommonCallData() {}
  virtual void Proceed(int ID = -1) = 0;
};

class AbstractAsyncClientCall {
 public:
  service_3::AsyncService* service_;
  Status status;
  ClientContext context;
  HelloRequest request;
  HelloReply reply;
  virtual ~AbstractAsyncClientCall() {}
  virtual void Proceed(bool = true) = 0;
};

class Rpc_3_CallData : public CommonCallData {
 public:
  std::lognormal_distribution<double>* rpc_3_proc_dist_;
  std::default_random_engine gen;
  Rpc_3_CallData(service_3::AsyncService* service, ServerCompletionQueue* cq,
                 std::lognormal_distribution<double>* rpc_3_proc_dist)
      : CommonCallData(service, cq), rpc_3_proc_dist_(rpc_3_proc_dist) {
    Proceed();
  }

  virtual void Proceed(int ID = -1) override {
    if (status_ == CREATE) {
      status_ = PROCESS;
      service_->Requestrpc_3(&ctx_, &request_, &responder_, cq_, cq_, this);
      auto seed =
          duration_cast<microseconds>(system_clock::now().time_since_epoch())
              .count();
      gen = std::default_random_engine(seed);
    } else if (status_ == PROCESS) {
      new Rpc_3_CallData(service_, cq_, rpc_3_proc_dist_);

      std::map<std::string, std::string> client_context_map;
      client_context_map.insert(
          pair<string, string>("uber-trace-id", request_.name()));
      TextMapReader reader(client_context_map);
      auto parent_span = opentracing::Tracer::Global()->Extract(reader);
      span = opentracing::Tracer::Global()->StartSpan(
          "rpc_3_server", {opentracing::ChildOf(parent_span->get())});

      double proc_time = (*rpc_3_proc_dist_)(gen);
      auto proc_t0 =
          duration_cast<microseconds>(system_clock::now().time_since_epoch())
              .count();
      while (true) {
        auto proc_t1 =
            duration_cast<microseconds>(system_clock::now().time_since_epoch())
                .count();
        if (proc_t1 - proc_t0 >= (int)(proc_time)) break;
      }

      span->Finish();
      responder_.Finish(reply_, Status::OK, this);
      status_ = DESTROY;
    } else if (status_ == FINISH) {
      span->Finish();
      responder_.Finish(reply_, Status::OK, this);
      status_ = DESTROY;
    } else {
      // GPR_ASSERT(status_ == DESTROY);
      delete this;
    }
  }
};

class ServerImpl final {
 public:
  std::unique_ptr<ServerCompletionQueue> cq_;
  std::unique_ptr<Server> server_;
  service_3::AsyncService service_;
  ~ServerImpl() {
    server_->Shutdown();
    cq_->Shutdown();
  }

  void Run() {
    json rpcs_json;
    json services_json;
    std::ifstream json_file;
    json_file.open("config/rpcs.json");
    if (json_file.is_open()) {
      json_file >> rpcs_json;
      json_file.close();
    } else {
      cout << "Cannot open rpcs_config.json" << endl;
      return;
    }
    json_file.open("config/services.json");
    if (json_file.is_open()) {
      json_file >> services_json;
      json_file.close();
    } else {
      cout << "Cannot open services_config.json" << endl;
      return;
    }

    int port = services_json["service_3"]["server_port"];
    string server_address("0.0.0.0:" + to_string(port));
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);
    cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();
    std::cout << "Server listening on " << server_address << std::endl;

    // currently distribution type not use, if const set std = 0
    string tmp_rpc_3_distribution_type =
        rpcs_json["rpc_3"]["distribution_type"];
    DistributionType rpc_3_distribution_type;
    if (tmp_rpc_3_distribution_type == "log_normal")
      rpc_3_distribution_type = log_normal;
    else
      rpc_3_distribution_type = constant;
    rpc_params rpc_3_params = {
        rpc_3_distribution_type,
        rpcs_json["rpc_3"]["pre_time_mean"],
        rpcs_json["rpc_3"]["pre_time_std"],
        rpcs_json["rpc_3"]["post_time_mean"],
        rpcs_json["rpc_3"]["post_time_std"],
        rpcs_json["rpc_3"]["proc_time_mean"],
        rpcs_json["rpc_3"]["proc_time_std"],
    };
    std::lognormal_distribution<double> rpc_3_proc_dist;
    double rpc_3_proc_time_mean = rpc_3_params.proc_time_mean;
    if (rpc_3_proc_time_mean != 0) {
      double rpc_3_proc_time_std = rpc_3_params.proc_time_std;
      double rpc_3_proc_m = log(
          rpc_3_proc_time_mean /
          sqrt(1 + pow(rpc_3_proc_time_std, 2) / pow(rpc_3_proc_time_mean, 2)));
      double rpc_3_proc_s = sqrt(
          log(1 + pow(rpc_3_proc_time_std, 2) / pow(rpc_3_proc_time_mean, 2)));
      rpc_3_proc_dist =
          std::lognormal_distribution<double>(rpc_3_proc_m, rpc_3_proc_s);
    }

    new Rpc_3_CallData(&service_, cq_.get(), &rpc_3_proc_dist);

    void* tag;
    bool ok;
    while (true) {
      GPR_ASSERT(cq_->Next(&tag, &ok));
      GPR_ASSERT(ok);
      std::thread([=] {
        static_cast<CommonCallData*>(tag)->Proceed();
      }).detach();
    }
  }
};

int main(int argc, char** argv) {
  SetUpTracer("config/jaeger-config.yml", "service_3");
  ServerImpl server;
  server.Run();
  return 0;
}
