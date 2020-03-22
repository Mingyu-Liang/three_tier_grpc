add_executable(
	service_3
	service_3.cpp
	${PROTO_SRCS}
	${GRPC_SRCS}
)

target_link_libraries(
	service_3
	gRPC::grpc++_reflection
	protobuf::libprotobuf
	jaegertracing
	nlohmann_json::nlohmann_json
)

install(TARGETS service_3 DESTINATION ./)
