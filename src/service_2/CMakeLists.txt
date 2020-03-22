add_executable(
	service_2
	service_2.cpp
	${PROTO_SRCS}
	${GRPC_SRCS}
)

target_link_libraries(
	service_2
	gRPC::grpc++_reflection
	protobuf::libprotobuf
	jaegertracing
	nlohmann_json::nlohmann_json
)

install(TARGETS service_2 DESTINATION ./)
