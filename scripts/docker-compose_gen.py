import os
import json
import sys

def main(argv):
  path_id = argv[0]
  image_name = argv[1]
  # services_replica = [1, 2, 2, 2, 4, 4, 4, 2, 2, 2, 6, 3]

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
    services_port = {}
    with open('../config/services.json') as load_services_file:
      services_json = json.load(load_services_file)
      for service in services:
        services_port[service] = services_json[service]["server_port"]
    # os.system("cp docker-compose_template.yml docker-compose.yml")
    f = open("docker-compose.yml", "a")
    print >> f, 'version: "3"'
    print >> f, 'services:'
    print >> f, '  jaeger:'
    print >> f, '    image: jaegertracing/all-in-one:latest'
    print >> f, '    hostname: jaeger'
    print >> f, '    ports:'
    print >> f, '      - 16610:16686'
    print >> f, '      - 6800:6831'
    print >> f, '    restart: always'
    print >> f, ''
    print >> f, '  frontend:'
    print >> f, '    image: ml2585/grpc_frontend'
    print >> f, '    hostname: frontend'
    print >> f, '    ports:'
    print >> f, '      - 9090:9090'
    print >> f, '    command: bash -c "source /etc/profile && cd /frontend && go run *.go"'
    print >> f, '    restart: always'
    print >> f, ''
    for service in services:
      service_id = int(service.split('_')[-1])
      print >> f, '  ' + service + ':'
      print >> f, '    image: ' + image_name
      print >> f, '    hostname: ' + service
      # print >> f, '    deploy:'
      # print >> f, '      replicas: ' + str(services_replica[service_id])
      # print >> f, '      resources:'
      # print >> f, '        limits:'
      # print >> f, "          cpus: '1'"
      # print >> f, '          memory: 1G'
      # print >> f, '    ports:'
      # print >> f, '      - ' + str(services_port[service]) + ":" + str(services_port[service])
      print >> f, '    restart: always'
      print >> f, '    entrypoint: ' + service
      print >> f, '    volumes:'
      print >> f, '      - ./config:/auto_grpc/config'
      print >> f, ""
    f.close()
    os.system("mv docker-compose.yml ../docker-compose.yml")

if __name__ == "__main__":
  main(sys.argv[1:])