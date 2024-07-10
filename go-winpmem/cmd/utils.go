package main

import (
	"context"
	"os"
	"os/signal"
	"syscall"
)

func install_sig_handler() (context.Context, context.CancelFunc) {
	quit := make(chan os.Signal, 1)
	signal.Notify(quit, syscall.SIGHUP,
		syscall.SIGINT,
		syscall.SIGTERM,
		syscall.SIGQUIT)

	ctx, cancel := context.WithCancel(context.Background())

	go func() {
		select {
		case <-quit:
			// Ordered shutdown now.
			cancel()

		case <-ctx.Done():
			return
		}
	}()

	return ctx, cancel
}
