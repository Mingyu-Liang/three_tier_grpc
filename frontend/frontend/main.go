package main

import (
	"fmt"
	"net/http"
	"log"

	"golang.org/x/net/http2"
	"golang.org/x/net/http2/h2c"
	"golang.org/x/net/context"
	"google.golang.org/grpc"

	"os"
)

var conn *grpc.ClientConn 
var err error
var request int 

const (
	address = "service_0:9000"
)

func checkErr(err error, msg string) {
	if err == nil {
		return
	}
	fmt.Printf("ERROR: %s: %s\n", msg, err)
	os.Exit(1)
}

func main() {
	conn, err = grpc.Dial(address, grpc.WithInsecure())
	if err != nil {
		log.Fatalf("did not connect: %v", err)
	}
	defer conn.Close()
	H2CServerUpgrade()
	//H2CServerPrior()
}

//H2CServerPrior()
func H2CServerUpgrade() {
	h2s := &http2.Server{}

	handler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		request = request + 1
		fmt.Println("request: ", request)
		c := NewService_0Client(conn)
		callMultipleAsyncRPC(c)
	})

	server := &http.Server{
		Addr:    "0.0.0.0:9090",
		Handler: h2c.NewHandler(handler, h2s),
	}

	fmt.Printf("Listening [0.0.0.0:9090]...\n")
	checkErr(server.ListenAndServe(), "while listening")
}

// package main

// import (
// 	"fmt"
// 	"log"
// 	"net/http"
	
// 	"golang.org/x/net/context"
// 	"google.golang.org/grpc"
// 	"golang.org/x/net/http2"
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
// 	// fmt.Println("frontend start")

// 	// http.HandleFunc("/", callRPC)

// 	// err := http.ListenAndServe("0.0.0.0:9090", nil)
// 	// if err != nil {
// 	// 		fmt.Println("cannot open server: ", err)
// 	// }
// 	var server http.Server

// 	// http2.VerboseLogs = false  //set true for verbose console output
// 	server.Addr = ":9090"

// 	log.Println("Starting server on localhost port 9090...")

// 	http2.ConfigureServer(&server, nil)

// 	http.HandleFunc("/", callRPC)

// 	log.Fatal(server.ListenAndServe())
// 	// err := http.ListenAndServe("0.0.0.0:9090", nil)
// 	// if err != nil {
// 	// 		fmt.Println("cannot open server: ", err)
// 	// }

// 	for {
// 	}
// }

func callMultipleAsyncRPC(c Service_0Client) {
	c.Rpc_0(context.Background(), &HelloRequest{Name: "rpc_0_client"})
	// for i := 0; i < 1; i++ {
	// 	go func() {
	// 		c.Rpc_0(context.Background(), &HelloRequest{Name: "rpc_0_client"})
	// 	}()
	// }
}
