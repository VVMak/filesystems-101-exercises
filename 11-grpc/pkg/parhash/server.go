package parhash

import (
	"context"
	"net"
	"sync"

	"github.com/pkg/errors"
	"golang.org/x/sync/semaphore"
	"google.golang.org/grpc"

	hashpb "fs101ex/pkg/gen/hashsvc"
	parhashpb "fs101ex/pkg/gen/parhashsvc"
	workgroup "fs101ex/pkg/workgroup"
)

type Config struct {
	ListenAddr   string
	BackendAddrs []string
	Concurrency  int
}

// Implement a server that responds to ParallelHash()
// as declared in /proto/parhash.proto.
//
// The implementation of ParallelHash() must not hash the content
// of buffers on its own. Instead, it must send buffers to backends
// to compute hashes. Buffers must be fanned out to backends in the
// round-robin fashion.
//
// For example, suppose that 2 backends are configured and ParallelHash()
// is called to compute hashes of 5 buffers. In this case it may assign
// buffers to backends in this way:
//
//	backend 0: buffers 0, 2, and 4,
//	backend 1: buffers 1 and 3.
//
// Requests to hash individual buffers must be issued concurrently.
// Goroutines that issue them must run within /pkg/workgroup/Wg. The
// concurrency within workgroups must be limited by Server.sem.
//
// WARNING: requests to ParallelHash() may be concurrent, too.
// Make sure that the round-robin fanout works in that case too,
// and evenly distributes the load across backends.
type Server struct {
	conf Config

	sem *semaphore.Weighted

	l    net.Listener
	wg   sync.WaitGroup
	stop context.CancelFunc
}

func New(conf Config) *Server {
	return &Server{
		conf: conf,
		sem:  semaphore.NewWeighted(int64(conf.Concurrency)),
	}
}

func (s *Server) Start(ctx context.Context) (err error) {
	defer func() { err = errors.Wrapf(err, "Start()") }()

	s.l, err = net.Listen("tcp", s.conf.ListenAddr)
	if err != nil {
		return err
	}

	ctx, s.stop = context.WithCancel(ctx)

	srv := grpc.NewServer()
	parhashpb.RegisterParallelHashSvcServer(srv, s)

	s.wg.Add(2)
	go func() {
		defer s.wg.Done()

		srv.Serve(s.l)
	}()
	go func() {
		defer s.wg.Done()

		<-ctx.Done()
		s.l.Close()
	}()

	return nil
}

func (s *Server) ListenAddr() string {
	return s.l.Addr().String()
}

func (s *Server) Stop() {
	s.stop()
	s.wg.Wait()
}

type ContextValues struct {
	data    []byte
	result  *[]byte
	backend string
}

type key int

func (s *Server) ParallelHash(ctx context.Context, req *parhashpb.ParHashReq) (resp *parhashpb.ParHashResp, err error) {
	backend := 0
	wg := workgroup.New(workgroup.Config{Sem: s.sem})

	result := make([][]byte, len(req.Data))

	for i := range req.Data {
		cur_ctx := context.WithValue(ctx, key(i), ContextValues{
			data:    req.Data[i],
			result:  &result[i],
			backend: s.conf.BackendAddrs[i],
		})
		wg.Go(cur_ctx, func(ctx context.Context) error {
			values, _ := ctx.Value(0).(ContextValues)
			conn, err := grpc.Dial(values.backend)
			if err != nil {
				return err
			}
			defer conn.Close()
			c := hashpb.NewHashSvcClient(conn)

			backend_resp, err := c.Hash(ctx, &hashpb.HashReq{Data: values.data})
			if err != nil {
				return err
			}
			*values.result = backend_resp.Hash
			return nil
		})
		backend += 1
		if backend == len(s.conf.BackendAddrs) {
			backend = 0
		}
	}
	err = wg.Wait()
	if err != nil {
		return nil, err
	}
	return &parhashpb.ParHashResp{Hashes: result}, nil
}
