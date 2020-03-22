import json
import os
import sys

image_name = "ml2585/three_tier_grpc"

def main(argv):
  path_id = argv[0]
  try:
    with open('../config/services.json') as load_services_file:
      services_json = json.load(load_services_file)
      for service in services_json:
        if os.path.exists("../src/" + service):
          os.system('rm -rf ../src/' + service)
    os.system('docker rmi ' + image_name)
    os.system('python proto_gen.py')
    os.system('python src_files_gen.py ' + path_id)
    os.system('python docker-compose_gen.py ' + path_id + ' ' + image_name)
    os.system('docker build -t ' + image_name + ' ..')
  except:
    pass

if __name__ == "__main__":
  main(sys.argv[1:])