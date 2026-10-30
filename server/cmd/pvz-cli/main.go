// SPDX-License-Identifier: LGPL-3.0-or-later
//
// pvz-cli is a small text client used to exercise the lobby during M0:
//
//	pvz-cli -addr 127.0.0.1:7777 -nick 玩家A
//
// then type: rooms / create 房间名 / join ABCDEF / ready / start / kick 2 /
// leave / quit. It measures round trip time the same way the game will.
package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"strconv"
	"strings"
	"sync/atomic"
	"time"

	"pvzlobby/internal/proto"
)

type cli struct {
	conn     net.Conn
	br       *bufio.Reader
	out      chan []byte
	seq      uint32
	rtt      atomic.Int64
	pid      int
	autoJoin bool
}

func main() {
	addr := flag.String("addr", "127.0.0.1:7777", "lobby address")
	nick := flag.String("nick", "玩家", "nickname")
	ver := flag.String("ver", "0.2.0", "client version")
	res := flag.String("res", "1073-cn", "resource version")
	protoVersion := flag.Int("proto", 1, "protocol version")
	flag.Parse()

	conn, err := net.Dial("tcp", *addr)
	if err != nil {
		log.Fatalf("dial: %v", err)
	}
	defer conn.Close()

	c := &cli{conn: conn, br: bufio.NewReader(conn), out: make(chan []byte, 64)}
	go c.writeLoop()
	go c.readLoop()

	c.sendMsg(proto.Msg{T: proto.THello, Ver: *ver, Proto: *protoVersion, Res: *res, Nick: *nick})

	go func() {
		ticker := time.NewTicker(2 * time.Second)
		defer ticker.Stop()
		for range ticker.C {
			c.sendPing()
		}
	}()

	scanner := bufio.NewScanner(os.Stdin)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}
		if line == "quit" || line == "exit" {
			return
		}
		c.command(line)
	}
}

func (c *cli) sendMsg(m proto.Msg) {
	body, err := json.Marshal(m)
	if err != nil {
		return
	}
	c.sendFrame(proto.TypeJSON, body)
}

func (c *cli) sendFrame(typ byte, payload []byte) {
	var buf bytes.Buffer
	if err := proto.WriteFrame(&buf, typ, payload); err != nil {
		return
	}
	select {
	case c.out <- buf.Bytes():
	default:
	}
}

func (c *cli) writeLoop() {
	for frame := range c.out {
		if _, err := c.conn.Write(frame); err != nil {
			return
		}
	}
}

func (c *cli) sendPing() {
	c.seq++
	payload := make([]byte, 8)
	seq := c.seq
	ms := uint32(time.Now().UnixMilli())
	payload[0] = byte(seq)
	payload[1] = byte(seq >> 8)
	payload[2] = byte(seq >> 16)
	payload[3] = byte(seq >> 24)
	payload[4] = byte(ms)
	payload[5] = byte(ms >> 8)
	payload[6] = byte(ms >> 16)
	payload[7] = byte(ms >> 24)
	c.sendFrame(proto.TypePing, payload)
}

func (c *cli) readLoop() {
	for {
		typ, payload, err := proto.ReadFrame(c.br)
		if err != nil {
			fmt.Println("[断开]", err)
			os.Exit(0)
		}
		switch typ {
		case proto.TypePong:
			if len(payload) >= 8 {
				sent := uint32(payload[4]) | uint32(payload[5])<<8 | uint32(payload[6])<<16 | uint32(payload[7])<<24
				rtt := int64(uint32(time.Now().UnixMilli()) - sent)
				c.rtt.Store(rtt)
				fmt.Printf("[延迟] %d ms\n", rtt)
				// Report it so peers see this player's latency too.
				c.sendMsg(proto.Msg{T: proto.TPrtt, Ping: int(rtt)})
			}
		case proto.TypeJSON:
			c.printMsg(payload)
		case proto.TypeInput, proto.TypeHash:
			fmt.Printf("[对局帧] type=%d %d 字节\n", typ, len(payload))
		}
	}
}

func (c *cli) printMsg(payload []byte) {
	var m proto.Msg
	if err := json.Unmarshal(payload, &m); err != nil {
		fmt.Println("[坏报文]", string(payload))
		return
	}

	switch m.T {
	case proto.TWelcome:
		c.pid = m.Pid
		fmt.Printf("[欢迎] pid=%d proto=%d res=%s\n", m.Pid, m.Proto, m.Res)
	case proto.TRooms:
		fmt.Printf("[房间列表] %d 个\n", len(m.Rooms))
		for _, r := range m.Rooms {
			fmt.Printf("  %s  %-16s 房主=%-8s %d/%d %s\n", r.Code, r.Name, r.Host, r.N, r.Maxp, r.State)
		}
		if c.autoJoin && len(m.Rooms) > 0 {
			c.autoJoin = false
			code := m.Rooms[0].Code
			fmt.Printf("[自动加入] %s\n", code)
			c.sendMsg(proto.Msg{T: proto.TJoin, Code: code})
		}
	case proto.TRoom:
		fmt.Printf("[房间] %s %q 房主=%d 状态=%s 种子=%d\n", m.Code, m.Name, m.Host, m.State, m.Seed)
		for _, mem := range m.Members {
			flags := ""
			if mem.Host {
				flags += " 房主"
			}
			if mem.Ready {
				flags += " 已准备"
			}
			fmt.Printf("  - pid=%d %s%s %dms\n", mem.Pid, mem.Nick, flags, mem.Ping)
		}
	case proto.TStart:
		fmt.Printf("[开局] 房间=%s 种子=%d 开始于=%d 资源=%s\n", m.Code, m.Seed, m.StartAt, m.Res)
	case proto.TLeft:
		fmt.Printf("[离开] 原因=%s\n", m.Reason)
	case proto.TChat:
		fmt.Printf("[聊天] %s: %s\n", m.Nick, m.Text)
	case proto.TErr:
		fmt.Printf("[错误] %d %s\n", m.Err, m.Text)
	default:
		fmt.Printf("[%s] %s\n", m.T, string(payload))
	}
}

func (c *cli) command(line string) {
	fields := strings.Fields(line)
	cmd := fields[0]
	arg := ""
	if len(fields) > 1 {
		arg = strings.Join(fields[1:], " ")
	}

	switch cmd {
	case "rooms":
		c.sendMsg(proto.Msg{T: proto.TRooms})
	case "create":
		c.sendMsg(proto.Msg{T: proto.TCreate, Name: arg, Mode: "versus", Maxp: 2})
	case "join":
		c.sendMsg(proto.Msg{T: proto.TJoin, Code: arg})
	case "joinlast":
		c.autoJoin = true
		c.sendMsg(proto.Msg{T: proto.TRooms})
	case "sleep":
		if ms, err := strconv.Atoi(arg); err == nil && ms > 0 {
			time.Sleep(time.Duration(ms) * time.Millisecond)
		}
	case "ready", "unready":
		v := cmd == "ready"
		c.sendMsg(proto.Msg{T: proto.TReady, Ready: &v})
	case "start":
		c.sendMsg(proto.Msg{T: proto.TStart})
	case "kick":
		pid, _ := strconv.Atoi(arg)
		c.sendMsg(proto.Msg{T: proto.TKick, Pid: pid})
	case "leave":
		c.sendMsg(proto.Msg{T: proto.TLeave})
	case "chat":
		c.sendMsg(proto.Msg{T: proto.TChat, Text: arg})
	case "rtt":
		fmt.Printf("[当前延迟] %d ms\n", c.rtt.Load())
	default:
		fmt.Println("命令: rooms | create [名字] | join 房间码 | joinlast | ready | unready | start | kick pid | leave | chat 文本 | sleep 毫秒 | rtt | quit")
	}
}
