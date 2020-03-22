package main

import (
	"fmt"
	"log"
	"net/http"
	// "golang.org/x/net/http2"
)

func main() {
	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprint(w, "Hello h2c")
	})
	s := &http.Server{
        Addr:    ":8972",
        Handler: mux,
    }
    // http2.ConfigureServer(s, &http2.Server{})
    log.Fatal(s.ListenAndServe())
}

// import (
// 	"fmt"
// 	"log"
// 	"net/http"
	
// 	"golang.org/x/net/context"
// 	"google.golang.org/grpc"
// )

// const (
// 	address = "service_0:9000"
// )

// var conn *grpc.ClientConn 
// var err error
// var request int 

// // func initJaeger(service string) (opentracing.Tracer, io.Closer) {
// // 	cfg := &config.Configuration{
// // 		Sampler: &config.SamplerConfig{
// // 			Type:  "const",
// // 			Param: 1,
// // 		},
// // 		Reporter: &config.ReporterConfig{
// // 			LogSpans:           true,
// // 			LocalAgentHostPort: "localhost:6831",
// // 		},
// // 	}
// // 	tracer, closer, err := cfg.New(service, config.Logger(jaeger.StdLogger))
// // 	if err != nil {
// // 		panic(fmt.Sprintf("ERROR: cannot init Jaeger: %v\n", err))
// // 	}
// // 	return tracer, closer
// // }

// func callRPC(w http.ResponseWriter, r *http.Request) {
// 	request = request + 1
// 	fmt.Println("request: ", request)
// 	c := NewService_0Client(conn)
// 	callMultipleAsyncRPC(c)
// }

// func main() {
// 	// Set up a connection to the server.
// 	conn, err = grpc.Dial(address, grpc.WithInsecure())
// 	if err != nil {
// 		log.Fatalf("did not connect: %v", err)
// 	}
// 	defer conn.Close()
// 	fmt.Println("frontend start")

// 	http.HandleFunc("/", callRPC)

// 	err := http.ListenAndServe("0.0.0.0:9090", nil)
// 	if err != nil {
// 			fmt.Println("cannot open server: ", err)
// 	}

// 	for {
// 	}
// }

// func callMultipleAsyncRPC(c Service_0Client) {
// 	c.Rpc_0(context.Background(), &HelloRequest{Name: "rpc_0_client"})
// 	// for i := 0; i < 1; i++ {
// 	// 	go func() {
// 	// 		c.Rpc_0(context.Background(), &HelloRequest{Name: "rpc_0_client"})
// 	// 	}()
// 	// }
// }
