// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Transport for a single lobby connection: one reader goroutine, one writer
// goroutine, and a bounded outbound queue. A client that cannot keep up is
// dropped instead of making the server allocate without bound.
package lobby

import (
	"bufio"
	"bytes"
	"encoding/json"
	"net"
	"sync"
	"sync/atomic"
	"time"

	"pvzlobby/internal/proto"
)

const (
	writeQueueSize  = 256
	inputRatePerSec = 240 // frames per second per client, match data only
)

type Client struct {
	hub  *Hub
	conn net.Conn
	br   *bufio.Reader
	out  chan []byte

	pid   int
	nick  string
	ver   string
	res   string
	hello bool
	room  *Room

	lastSeen atomic.Int64 // unix milliseconds
	timedOut atomic.Bool  // set by the reaper so the room learns why we left

	mu       sync.Mutex
	inputWin int
	winStart time.Time

	writeMu   sync.Mutex
	closeOnce sync.Once
	closed    chan struct{}
}

func newClient(h *Hub, conn net.Conn, pid int) *Client {
	return &Client{
		hub:      h,
		conn:     conn,
		br:       bufio.NewReaderSize(conn, 4096),
		out:      make(chan []byte, writeQueueSize),
		pid:      pid,
		nick:     defaultNick(pid),
		closed:   make(chan struct{}),
		winStart: time.Now(),
	}
}

func defaultNick(pid int) string {
	return "玩家" + itoa(pid)
}

func itoa(v int) string {
	if v == 0 {
		return "0"
	}
	var buf [12]byte
	i := len(buf)
	for v > 0 {
		i--
		buf[i] = byte('0' + v%10)
		v /= 10
	}
	return string(buf[i:])
}

// sendMsg marshals a control message and queues it.
func (c *Client) sendMsg(m proto.Msg) {
	body, err := json.Marshal(m)
	if err != nil {
		return
	}
	c.sendFrame(proto.TypeJSON, body)
}

// sendFrame queues one already-serialized payload.
func (c *Client) sendFrame(typ byte, payload []byte) {
	var buf bytes.Buffer
	if err := proto.WriteFrame(&buf, typ, payload); err != nil {
		return
	}

	select {
	case c.out <- buf.Bytes():
	case <-c.closed:
	default:
		// Slow consumer: the queue is full, so this connection is hopeless.
		c.close()
	}
}

func (c *Client) close() {
	c.closeOnce.Do(func() {
		close(c.closed)
		_ = c.conn.Close()
	})
}

func (c *Client) writeLoop() {
	for {
		select {
		case frame := <-c.out:
			c.writeMu.Lock()
			_ = c.conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
			_, err := c.conn.Write(frame)
			c.writeMu.Unlock()
			if err != nil {
				c.close()
				return
			}
		case <-c.closed:
			return
		}
	}
}

// reject sends one final message and closes the connection. It writes
// synchronously so the client actually receives the reason before the socket
// goes away; ordinary sends stay asynchronous.
func (c *Client) reject(m proto.Msg) {
	body, err := json.Marshal(m)
	if err == nil {
		var buf bytes.Buffer
		if proto.WriteFrame(&buf, proto.TypeJSON, body) == nil {
			c.writeMu.Lock()
			_ = c.conn.SetWriteDeadline(time.Now().Add(2 * time.Second))
			_, _ = c.conn.Write(buf.Bytes())
			c.writeMu.Unlock()
		}
	}
	c.close()
}

func (c *Client) readLoop() {
	defer c.hub.drop(c)

	for {
		typ, payload, err := proto.ReadFrame(c.br)
		if err != nil {
			return
		}
		c.lastSeen.Store(time.Now().UnixMilli())

		if !c.hub.dispatch(c, typ, payload) {
			return
		}
	}
}

// allowInput implements a simple token bucket for match traffic.
func (c *Client) allowInput() bool {
	c.mu.Lock()
	defer c.mu.Unlock()

	now := time.Now()
	if now.Sub(c.winStart) >= time.Second {
		c.winStart = now
		c.inputWin = 0
	}
	c.inputWin++
	return c.inputWin <= inputRatePerSec
}

func (c *Client) touch() {
	c.lastSeen.Store(time.Now().UnixMilli())
}
