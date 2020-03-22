FROM ml2585/auto_grpc:test

ARG NUM_CPUS=40

COPY ./ /auto_grpc
RUN cd /auto_grpc \
  && rm -rf build \
  && mkdir -p build \
  && cd build \
  && cmake .. \
  && make -j${NUM_CPUS} \
  && make install

WORKDIR /auto_grpc