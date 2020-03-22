FROM ubuntu:16.04

# COPY ./frontend /frontend

RUN apt-get update \
  && apt-get install -y wget \
  # Install golang
  && cd /tmp \
  && wget https://dl.google.com/go/go1.13.8.linux-amd64.tar.gz \
  && tar -C /usr/local -xzf go1.13.8.linux-amd64.tar.gz \
  && echo export PATH=$PATH:/usr/local/go/bin >> /etc/profile \
  && . /etc/profile
  # Build and run frontend
  # && cd /frontend \
  # && go mod init frontend \
  # && go build *.go 