import os
import json
import sys

def main(argv):
  path_id = argv[0]
  with open('../config/paths.json') as load_path_file:
    path_json = json.load(load_path_file)
    path = path_json["path_" + str(path_id)]
    edges = path["edges"]
    rpcs = []
    for edge in edges:
      rpcs.append(edge[0])
      rpcs.append(edge[1])
    rpcs = list(set(rpcs))
    services = []
    with open('../config/rpcs.json') as load_rpcs_file:
      rpcs_json = json.load(load_rpcs_file)
      for rpc in rpcs:
        services.append(rpcs_json["rpc_" + str(rpc)]["client"])
        services.append(rpcs_json["rpc_" + str(rpc)]["server"])
    services = list(set(services))
    services.remove("nginx")
    services.sort()
    services_related = {}
    with open('../config/services.json') as load_services_file:
      services_json = json.load(load_services_file)
      for service in services:
        related_services = [service]
        for i in services_json[service]["rpcs_send"]:
          related_services.append(rpcs_json["rpc_" + str(i)]["server"])
        services_related[service] = related_services
    f = open("cog.txt", "w")
    for service in services:
      os.system('mkdir -p ../src/' + service)
      print >> f, "transfer_grpc.cpp -d -D serviceName='" + service + "' -D pathName='path_" + str(path_id) \
              + "' -o ../src/" + service + "/" + service + ".cpp" 
      with open("../src/" + service + "/CMakeLists.txt", "w") as cmake_f:
        print >> cmake_f, "add_executable("
        print >> cmake_f, '\t' + service
        print >> cmake_f, '\t' + service + '.cpp'
        print >> cmake_f, "\t${PROTO_SRCS}"
        print >> cmake_f, "\t${GRPC_SRCS}"
        print >> cmake_f, ")"
        print >> cmake_f, ""
        print >> cmake_f, "target_link_libraries("
        print >> cmake_f, '\t' + service
        print >> cmake_f, "\tgRPC::grpc++_reflection"
        print >> cmake_f, "\tprotobuf::libprotobuf"
        print >> cmake_f, "\tjaegertracing"
        print >> cmake_f, "\tnlohmann_json::nlohmann_json"
        # print >> cmake_f, "\t/usr/local/lib/libjaegertracing.so"
        print >> cmake_f, ")"
        print >> cmake_f, ""
        print >> cmake_f, 'install(TARGETS ' + service + ' DESTINATION ./)'
    f.close()
    f = open("../src/CMakeLists.txt", "w")
    for service in services:
      print >> f, 'add_subdirectory(' + service + ')'
    f.close()
    os.system('cog @cog.txt')
    for service in services:
      os.system('clang-format-3.8 -i ../src/' + service + '/' + service + '.cpp -style=Google')
    os.system('rm cog.txt')

if __name__ == "__main__":
  main(sys.argv[1:])