// SPDX-License-Identifier: LGPL-3.0-or-later
//
// pvz-lobby is the multiplayer lobby server for PvZ-Portable.
package main

import (
	"flag"
	"log"
	"net"
	"os"
	"os/signal"
	"syscall"
	"time"

	"pvzlobby/internal/lobby"
)

func main() {
	addr := flag.String("listen", ":7777", "TCP listen address")
	protoVersion := flag.Int("proto", 1, "protocol version accepted from clients")
	resourceVersion := flag.String("res", "1073-cn", "resource version players must match to join a room")
	maxRooms := flag.Int("max-rooms", 5000, "maximum number of rooms")
	idle := flag.Duration("idle", 30*time.Second, "disconnect clients that stay silent this long")
	flag.Parse()

	hub := lobby.New(lobby.Config{
		ProtoVersion:    *protoVersion,
		ResourceVersion: *resourceVersion,
		MaxRooms:        *maxRooms,
		IdleTimeout:     *idle,
	})

	ln, err := net.Listen("tcp", *addr)
	if err != nil {
		log.Fatalf("listen %s: %v", *addr, err)
	}
	log.Printf("pvz-lobby listening on %s (proto=%d res=%s)", ln.Addr(), *protoVersion, *resourceVersion)

	go func() {
		sig := make(chan os.Signal, 1)
		signal.Notify(sig, os.Interrupt, syscall.SIGTERM)
		<-sig
		log.Printf("shutting down")
		hub.Close()
	}()

	if err := hub.Serve(ln); err != nil {
		log.Fatalf("serve: %v", err)
	}
}
