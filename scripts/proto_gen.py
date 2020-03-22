import json
import os
from collections import OrderedDict

f = open("../protos/helloworld.proto", 'w')
print >> f, "syntax = \"proto3\";"
print >> f, ""
print >> f, "option java_multiple_files = true;"
print >> f, "option java_package = \"io.grpc.examples.helloworld\";"
print >> f, "option java_outer_classname = \"HelloWorldProto\";"
print >> f, "option objc_class_prefix = \"HLW\";"
print >> f, ""
print >> f, "package helloworld;"
print >> f, ""
with open("../config/services.json",'r') as load_services_f:
  services_json = json.load(load_services_f, object_pairs_hook=OrderedDict)
  print >> f, "message HelloRequest {"
  print >> f, "  string name = 1;"
  print >> f, "}"
  print >> f, "message HelloReply {"
  print >> f, "  string message = 1;"
  print >> f, "}"
  print >> f, ""
  for service in services_json:
    print >> f, "service " + service + "{"
    for rpc in services_json[service]["rpcs_receive"]:
      print >> f, "  rpc rpc_" + str(rpc) + " (HelloRequest) returns (HelloReply) {}"
    print >> f, "}"
    print >> f, ""
f.close()

os.system('protoc -I ../protos --cpp_out=../protos/ ../protos/helloworld.proto')
os.system('protoc -I ../protos --grpc_out=../protos/ --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` ../protos/helloworld.proto')