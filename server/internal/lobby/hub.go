// SPDX-License-Identifier: LGPL-3.0-or-later
//
// The hub owns every room and connection. All mutation happens under one
// mutex and never blocks on network I/O: sending only enqueues into a bounded
// per-client channel, so a stalled peer cannot freeze the lobby.
package lobby

import (
	"crypto/rand"
	"encoding/binary"
	"encoding/json"
	"net"
	"sort"
	"strings"
	"sync"
	"time"

	"pvzlobby/internal/proto"
)

const (
	defaultMaxRooms    = 5000
	defaultIdleTimeout = 30 * time.Second
	reapInterval       = 5 * time.Second
	startDelayMillis   = 2000
	maxRoomsInList     = 100
)

// Config is the server configuration.
type Config struct {
	ProtoVersion    int
	ResourceVersion string
	MaxRooms        int
	IdleTimeout     time.Duration
}

func (c *Config) applyDefaults() {
	if c.ProtoVersion == 0 {
		c.ProtoVersion = 1
	}
	if c.ResourceVersion == "" {
		c.ResourceVersion = "1073-cn"
	}
	if c.MaxRooms <= 0 {
		c.MaxRooms = defaultMaxRooms
	}
	if c.IdleTimeout <= 0 {
		c.IdleTimeout = defaultIdleTimeout
	}
}

// Hub is the lobby server state.
type Hub struct {
	mu      sync.Mutex
	cfg     Config
	clients map[int]*Client
	rooms   map[string]*Room
	nextPid int
	ln      net.Listener
	stopped chan struct{}
	once    sync.Once
}

// New creates an empty hub.
func New(cfg Config) *Hub {
	cfg.applyDefaults()
	return &Hub{
		cfg:     cfg,
		clients: map[int]*Client{},
		rooms:   map[string]*Room{},
		stopped: make(chan struct{}),
	}
}

// Serve accepts connections until the listener is closed.
func (h *Hub) Serve(ln net.Listener) error {
	h.mu.Lock()
	h.ln = ln
	h.mu.Unlock()

	go h.reapLoop()

	for {
		conn, err := ln.Accept()
		if err != nil {
			select {
			case <-h.stopped:
				return nil
			default:
				return err
			}
		}

		h.mu.Lock()
		if len(h.clients) >= h.cfg.MaxRooms*2 {
			h.mu.Unlock()
			_ = conn.Close()
			continue
		}
		h.nextPid++
		c := newClient(h, conn, h.nextPid)
		h.clients[c.pid] = c
		h.mu.Unlock()

		go c.writeLoop()
		go c.readLoop()
	}
}

// Close stops the hub and every connection.
func (h *Hub) Close() {
	h.once.Do(func() {
		close(h.stopped)
		h.mu.Lock()
		if h.ln != nil {
			_ = h.ln.Close()
		}
		for _, c := range h.clients {
			c.close()
		}
		h.mu.Unlock()
	})
}

func (h *Hub) reapLoop() {
	ticker := time.NewTicker(reapInterval)
	defer ticker.Stop()

	for {
		select {
		case <-h.stopped:
			return
		case now := <-ticker.C:
			cutoff := now.Add(-h.cfg.IdleTimeout).UnixMilli()
			var dead []*Client

			h.mu.Lock()
			for _, c := range h.clients {
				if c.lastSeen.Load() < cutoff {
					dead = append(dead, c)
				}
			}
			h.mu.Unlock()

			for _, c := range dead {
				c.timedOut.Store(true)
				c.close()
			}
		}
	}
}

// dispatch handles one frame. Returning false closes the connection.
func (h *Hub) dispatch(c *Client, typ byte, payload []byte) bool {
	switch typ {
	case proto.TypePing:
		c.sendFrame(proto.TypePong, payload)
		return true
	case proto.TypeInput, proto.TypeHash:
		return h.relay(c, typ, payload)
	case proto.TypeJSON:
		return h.handleJSON(c, payload)
	default:
		return false
	}
}

func (h *Hub) handleJSON(c *Client, payload []byte) bool {
	var m proto.Msg
	if err := json.Unmarshal(payload, &m); err != nil {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrBadRequest, Text: "报文格式错误"})
		return true
	}

	if !c.hello && m.T != proto.THello {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrBadRequest, Text: "请先发送 hello"})
		return true
	}

	switch m.T {
	case proto.THello:
		h.onHello(c, &m)
	case proto.TRooms:
		h.onRooms(c)
	case proto.TCreate:
		h.onCreate(c, &m)
	case proto.TJoin:
		h.onJoin(c, &m)
	case proto.TLeave:
		h.onLeave(c)
	case proto.TKick:
		h.onKick(c, &m)
	case proto.TReady:
		h.onReady(c, &m)
	case proto.TStart:
		h.onStart(c)
	case proto.TChat:
		h.onChat(c, &m)
	case proto.TPrtt:
		h.onPingReport(c, &m)
	default:
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrBadRequest, Text: "未知消息 " + m.T})
	}
	return true
}

func (h *Hub) onHello(c *Client, m *proto.Msg) {
	if c.hello {
		return
	}
	if m.Proto != h.cfg.ProtoVersion {
		c.reject(proto.Msg{T: proto.TErr, Err: proto.ErrVersion,
			Text: "协议版本不符，服务器要求 " + itoa(h.cfg.ProtoVersion)})
		return
	}

	c.hello = true
	c.ver = m.Ver
	c.res = m.Res
	if nick := sanitize(m.Nick, 16); nick != "" {
		c.nick = nick
	}

	c.sendMsg(proto.Msg{
		T:          proto.TWelcome,
		Pid:        c.pid,
		Proto:      h.cfg.ProtoVersion,
		Res:        h.cfg.ResourceVersion,
		ServerTime: time.Now().UnixMilli(),
	})
}

func (h *Hub) onRooms(c *Client) {
	h.mu.Lock()
	list := make([]proto.RoomInfo, 0, len(h.rooms))
	for _, r := range h.rooms {
		if r.State == proto.StateClosed {
			continue
		}
		list = append(list, h.roomInfoLocked(r))
	}
	h.mu.Unlock()

	sort.Slice(list, func(i, j int) bool { return list[i].Code < list[j].Code })
	if len(list) > maxRoomsInList {
		list = list[:maxRoomsInList]
	}
	c.sendMsg(proto.Msg{T: proto.TRooms, Rooms: list})
}

func (h *Hub) onCreate(c *Client, m *proto.Msg) {
	h.mu.Lock()
	defer h.mu.Unlock()

	if c.room != nil {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrInGame, Text: "你已经在房间里"})
		return
	}
	if len(h.rooms) >= h.cfg.MaxRooms {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrRoomFull, Text: "服务器房间已满"})
		return
	}

	code, ok := newRoomCode(func(code string) bool {
		_, taken := h.rooms[code]
		return taken
	})
	if !ok {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrBadRequest, Text: "房间码生成失败"})
		return
	}

	name := sanitize(m.Name, 24)
	if name == "" {
		name = c.nick + "的房间"
	}

	maxP := m.Maxp
	if maxP < 2 || maxP > 4 {
		maxP = 2
	}
	mode := m.Mode
	if mode == "" {
		mode = "versus"
	}

	r := newRoom(code, name, c.pid, mode, maxP, c.res)
	h.rooms[code] = r
	c.room = r

	c.sendMsg(proto.Msg{T: proto.TRoom, Code: r.Code, Name: r.Name, Host: r.Host,
		Mode: r.Mode, Maxp: r.MaxP, State: r.State, Res: r.Res, Members: h.memberListLocked(r)})
}

func (h *Hub) onJoin(c *Client, m *proto.Msg) {
	h.mu.Lock()
	defer h.mu.Unlock()

	if c.room != nil {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrInGame, Text: "你已经在房间里"})
		return
	}

	code := strings.ToUpper(strings.TrimSpace(m.Code))
	r, ok := h.rooms[code]
	if !ok {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrNoRoom, Text: "房间不存在"})
		return
	}
	if r.State == proto.StatePlaying {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrInGame, Text: "对局已经开始"})
		return
	}
	if len(r.Order) >= r.MaxP {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrRoomFull, Text: "房间已满"})
		return
	}
	if r.Res != c.res {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrResource,
			Text: "资源版本不符：房主 " + r.Res + "，你是 " + c.res})
		return
	}

	r.Order = append(r.Order, c.pid)
	r.refreshState()
	c.room = r
	h.broadcastRoomLocked(r)
}

func (h *Hub) onLeave(c *Client) {
	h.mu.Lock()
	defer h.mu.Unlock()

	if c.room == nil {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrNoRoom, Text: "你不在房间里"})
		return
	}
	h.leaveLocked(c, "leave", true)
}

func (h *Hub) onKick(c *Client, m *proto.Msg) {
	h.mu.Lock()
	defer h.mu.Unlock()

	r := c.room
	if r == nil {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrNoRoom, Text: "你不在房间里"})
		return
	}
	if r.Host != c.pid {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrNotHost, Text: "只有房主可以踢人"})
		return
	}
	if m.Pid == c.pid {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrBadRequest, Text: "不能踢自己"})
		return
	}
	target := h.clients[m.Pid]
	if target == nil || target.room != r {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrBadRequest, Text: "该玩家不在房间里"})
		return
	}

	h.leaveLocked(target, "kick", true)
}

func (h *Hub) onReady(c *Client, m *proto.Msg) {
	h.mu.Lock()
	defer h.mu.Unlock()

	r := c.room
	if r == nil {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrNoRoom, Text: "你不在房间里"})
		return
	}
	ready := m.Ready != nil && *m.Ready
	r.Ready[c.pid] = ready
	r.refreshState()
	h.broadcastRoomLocked(r)
}

func (h *Hub) onStart(c *Client) {
	h.mu.Lock()
	defer h.mu.Unlock()

	r := c.room
	if r == nil {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrNoRoom, Text: "你不在房间里"})
		return
	}
	if r.Host != c.pid {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrNotHost, Text: "只有房主可以开始"})
		return
	}
	if len(r.Order) < 2 {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrTooFew, Text: "至少需要两名玩家"})
		return
	}
	if !r.allReady() {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrNotReady, Text: "有玩家尚未准备"})
		return
	}

	var seedBuf [4]byte
	if _, err := rand.Read(seedBuf[:]); err != nil {
		c.sendMsg(proto.Msg{T: proto.TErr, Err: proto.ErrBadRequest, Text: "随机种子生成失败"})
		return
	}
	r.Seed = binary.LittleEndian.Uint32(seedBuf[:])
	r.State = proto.StatePlaying

	startAt := time.Now().UnixMilli() + startDelayMillis
	for _, pid := range r.Order {
		peer := h.clients[pid]
		if peer == nil {
			continue
		}
		peer.sendMsg(proto.Msg{T: proto.TStart, Code: r.Code, Seed: r.Seed,
			StartAt: startAt, Res: r.Res, ServerTime: time.Now().UnixMilli()})
	}
	h.broadcastRoomLocked(r)
}

func (h *Hub) onChat(c *Client, m *proto.Msg) {
	text := sanitize(m.Text, 120)
	if text == "" {
		return
	}

	h.mu.Lock()
	defer h.mu.Unlock()

	r := c.room
	if r == nil {
		return
	}
	for _, pid := range r.Order {
		peer := h.clients[pid]
		if peer != nil {
			peer.sendMsg(proto.Msg{T: proto.TChat, Pid: c.pid, Nick: c.nick, Text: text})
		}
	}
}

// onPingReport stores the round trip time a client measured for itself and,
// when it moved enough to matter, refreshes the room state so peers see it.
func (h *Hub) onPingReport(c *Client, m *proto.Msg) {
	if m.Ping < 0 || m.Ping > 5000 {
		return
	}

	h.mu.Lock()
	defer h.mu.Unlock()

	r := c.room
	if r == nil {
		return
	}
	old, had := r.Ping[c.pid]
	r.Ping[c.pid] = m.Ping
	diff := m.Ping - old
	if diff < 0 {
		diff = -diff
	}
	if !had || diff >= 10 {
		h.broadcastRoomLocked(r)
	}
}

// relay forwards match traffic to the other member of the room.
func (h *Hub) relay(c *Client, typ byte, payload []byte) bool {
	if !c.allowInput() {
		return false
	}

	h.mu.Lock()
	defer h.mu.Unlock()

	r := c.room
	if r == nil || r.State != proto.StatePlaying {
		return true
	}
	for _, pid := range r.Order {
		if pid == c.pid {
			continue
		}
		if peer := h.clients[pid]; peer != nil {
			peer.sendFrame(typ, payload)
		}
	}
	return true
}

// drop cleans up after a disconnected client. Called from readLoop's defer.
func (h *Hub) drop(c *Client) {
	reason := "disconnect"
	if c.timedOut.Load() {
		reason = "timeout"
	}

	h.mu.Lock()
	if c.room != nil {
		h.leaveLocked(c, reason, false)
	}
	delete(h.clients, c.pid)
	h.mu.Unlock()

	c.close()
}

// leaveLocked removes c from its room. h.mu must be held.
func (h *Hub) leaveLocked(c *Client, reason string, notifySelf bool) {
	r := c.room
	if r == nil {
		return
	}
	c.room = nil
	r.remove(c.pid)

	if notifySelf {
		c.sendMsg(proto.Msg{T: proto.TLeft, Reason: reason})
	}

	if len(r.Order) == 0 {
		delete(h.rooms, r.Code)
		return
	}

	if r.State == proto.StatePlaying {
		// A match cannot continue with a missing player: tell the rest and
		// dissolve the room. The client decides who won.
		for _, pid := range r.Order {
			peer := h.clients[pid]
			if peer == nil {
				continue
			}
			peer.room = nil
			peer.sendMsg(proto.Msg{T: proto.TLeft, Reason: reason, Pid: c.pid})
		}
		delete(h.rooms, r.Code)
		return
	}

	r.refreshState()
	h.broadcastRoomLocked(r)
}

func (h *Hub) broadcastRoomLocked(r *Room) {
	members := h.memberListLocked(r)
	for _, pid := range r.Order {
		peer := h.clients[pid]
		if peer == nil {
			continue
		}
		peer.sendMsg(proto.Msg{
			T: proto.TRoom, Code: r.Code, Name: r.Name, Host: r.Host, Mode: r.Mode,
			Maxp: r.MaxP, State: r.State, Res: r.Res, Seed: r.Seed, Members: members,
		})
	}
}

func (h *Hub) memberListLocked(r *Room) []proto.MemberInfo {
	out := make([]proto.MemberInfo, 0, len(r.Order))
	for _, pid := range r.Order {
		peer := h.clients[pid]
		if peer == nil {
			continue
		}
		out = append(out, proto.MemberInfo{
			Pid:   pid,
			Nick:  peer.nick,
			Ready: r.Ready[pid],
			Host:  r.Host == pid,
			Ping:  r.Ping[pid],
			Res:   peer.res,
		})
	}
	return out
}

func (h *Hub) roomInfoLocked(r *Room) proto.RoomInfo {
	host := ""
	if peer := h.clients[r.Host]; peer != nil {
		host = peer.nick
	}
	return proto.RoomInfo{
		Code:  r.Code,
		Name:  r.Name,
		Host:  host,
		Mode:  r.Mode,
		N:     len(r.Order),
		Maxp:  r.MaxP,
		State: r.State,
		Res:   r.Res,
	}
}

// sanitize trims, drops control characters and truncates to maxRunes.
func sanitize(s string, maxRunes int) string {
	s = strings.TrimSpace(s)
	if s == "" {
		return ""
	}

	var b strings.Builder
	count := 0
	for _, r := range s {
		if r < 0x20 || r == 0x7f {
			continue
		}
		if count >= maxRunes {
			break
		}
		b.WriteRune(r)
		count++
	}
	return b.String()
}
