/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include <grpc/support/log.h>
#include <thread>
#include "tracing.h"

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

using namespace std;
using namespace helloworld;
using namespace grpc;

class AbstractAsyncClientCall
{
public:
	virtual ~AbstractAsyncClientCall(){}
	HelloReply reply;
	ClientContext context;
	Status status;
	virtual void Proceed(bool = true) = 0;
};

class AsyncClientCall: public AbstractAsyncClientCall
{
	std::unique_ptr<ClientAsyncResponseReader<HelloReply>> responder;
	std::unique_ptr<opentracing::Span> span;
public:
	AsyncClientCall(HelloRequest& request, CompletionQueue& cq_, std::unique_ptr<service_0::Stub>& stub_):AbstractAsyncClientCall()
	{
		std::map<std::string, std::string> writer_text_map;
    TextMapWriter writer(writer_text_map);
    span = opentracing::Tracer::Global()->StartSpan("rpc_0_client");
    opentracing::Tracer::Global()->Inject(span->context(), writer);
		request.set_name(writer_text_map.begin()->second);
		// for(auto it = writer_text_map.begin(); it != writer_text_map.end(); it++) {
		// 	cout << it->first << " " << it->second << endl;
		// 	context.AddMetadata(it->first, it->second);
		// }
		cout << "send rpc" << endl;
		responder = stub_->Asyncrpc_0(&context, request, &cq_);
		//responder = stub_->PrepareAsyncSayHello(&call->context, request, &cq_);
		// responder->StartCall();
		responder->Finish(&reply, &status, (void*)this);
	}

	virtual void Proceed(bool ok = true) override
	{
		GPR_ASSERT(ok);
		span->Finish();
		if(status.ok())
			cout << "First success" << endl;
		delete this;
	}
};

class GreeterClient
{
public:
	explicit GreeterClient(std::shared_ptr<Channel> channel)
			: stub_(service_0::NewStub(channel)) {}

	// Assembles the client's payload and sends it to the server.
	void SayHello(const std::string &user)
	{
		// Data we are sending to the server.
		HelloRequest request;
		// request.set_name(user);
		AsyncClientCall *call = new AsyncClientCall(request, cq_, stub_);
	}

	// Loop while listening for completed responses.
	// Prints out the response from the server.
	void AsyncCompleteRpc()
	{
		void *got_tag;
		bool ok = false;

		// Block until the next result is available in the completion queue "cq".
		while (cq_.Next(&got_tag, &ok))
		{
			// The tag in this example is the memory location of the call object
			AbstractAsyncClientCall *call = static_cast<AbstractAsyncClientCall *>(got_tag);
			call->Proceed(ok);
		}
		cout << "Completion queue is shutting down." << endl;
	}

private:
	std::unique_ptr<service_0::Stub> stub_;
	CompletionQueue cq_;
};

int main(int argc, char **argv)
{
	SetUpTracer("/home/ml2585/auto_grpc/config/jaeger.yml", "client");
	// Instantiate the client. It requires a channel, out of which the actual RPCs
	// are created. This channel models a connection to an endpoint (in this case,
	// localhost at port 50051). We indicate that the channel isn't authenticated
	// (use of InsecureChannelCredentials()).
	GreeterClient greeter(grpc::CreateChannel(
			"0.0.0.0:9000", grpc::InsecureChannelCredentials()));

	// Spawn reader thread that loops indefinitely
	std::thread thread_ = std::thread(&GreeterClient::AsyncCompleteRpc, &greeter);
	greeter.SayHello("world");
	// greeter.GladToSeeMe("client");

	// for (int i = 0; i < 100; i++)
	// {
	// 	std::string user("world " + std::to_string(i));
	// 	greeter.SayHello(user); // The actual RPC call!
	// }

	// std::cout << "Press control-c to quit" << std::endl
	// 					<< std::endl;
	thread_.join(); //blocks forever

	return 0;
}
