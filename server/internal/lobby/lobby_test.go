// SPDX-License-Identifier: LGPL-3.0-or-later
package lobby_test

import (
	"bufio"
	"bytes"
	"encoding/json"
	"net"
	"reflect"
	"testing"
	"time"

	"pvzlobby/internal/lobby"
	"pvzlobby/internal/proto"
)

const testRes = "1073-cn"

type testClient struct {
	t    *testing.T
	conn net.Conn
	br   *bufio.Reader
}

func startServer(t *testing.T, res string) string {
	t.Helper()

	return startServerWith(t, lobby.Config{ProtoVersion: 1, ResourceVersion: res})
}

func startServerWith(t *testing.T, cfg lobby.Config) string {
	t.Helper()

	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}

	hub := lobby.New(cfg)
	go func() { _ = hub.Serve(ln) }()
	t.Cleanup(hub.Close)

	return ln.Addr().String()
}

func dial(t *testing.T, addr string) *testClient {
	t.Helper()

	conn, err := net.Dial("tcp", addr)
	if err != nil {
		t.Fatalf("dial: %v", err)
	}
	t.Cleanup(func() { _ = conn.Close() })

	return &testClient{t: t, conn: conn, br: bufio.NewReader(conn)}
}

func (c *testClient) send(m proto.Msg) {
	c.t.Helper()

	body, err := json.Marshal(m)
	if err != nil {
		c.t.Fatalf("marshal: %v", err)
	}
	var buf bytes.Buffer
	if err := proto.WriteFrame(&buf, proto.TypeJSON, body); err != nil {
		c.t.Fatalf("write frame: %v", err)
	}
	if _, err := c.conn.Write(buf.Bytes()); err != nil {
		c.t.Fatalf("write: %v", err)
	}
}

func (c *testClient) sendFrame(typ byte, payload []byte) {
	c.t.Helper()

	var buf bytes.Buffer
	if err := proto.WriteFrame(&buf, typ, payload); err != nil {
		c.t.Fatalf("write frame: %v", err)
	}
	if _, err := c.conn.Write(buf.Bytes()); err != nil {
		c.t.Fatalf("write: %v", err)
	}
}

// next returns the next frame, failing the test on timeout.
func (c *testClient) next() (byte, []byte) {
	c.t.Helper()

	_ = c.conn.SetReadDeadline(time.Now().Add(3 * time.Second))
	typ, payload, err := proto.ReadFrame(c.br)
	if err != nil {
		c.t.Fatalf("read: %v", err)
	}
	return typ, payload
}

// want reads until a control message with the given type arrives.
func (c *testClient) want(want string) proto.Msg {
	c.t.Helper()

	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		typ, payload := c.next()
		if typ != proto.TypeJSON {
			continue
		}
		var m proto.Msg
		if err := json.Unmarshal(payload, &m); err != nil {
			c.t.Fatalf("unmarshal: %v", err)
		}
		if m.T == proto.TErr {
			c.t.Fatalf("unexpected error %d: %s", m.Err, m.Text)
		}
		if m.T == want {
			return m
		}
	}
	c.t.Fatalf("timed out waiting for %q", want)
	return proto.Msg{}
}

// wantRoomState reads room updates until one reports the given state.
func (c *testClient) wantRoomState(state string) proto.Msg {
	c.t.Helper()

	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		m := c.want(proto.TRoom)
		if m.State == state {
			return m
		}
	}
	c.t.Fatalf("timed out waiting for room state %q", state)
	return proto.Msg{}
}

// wantErr reads until an error message arrives.
func (c *testClient) wantErr() proto.Msg {
	c.t.Helper()

	for i := 0; i < 8; i++ {
		typ, payload := c.next()
		if typ != proto.TypeJSON {
			continue
		}
		var m proto.Msg
		if err := json.Unmarshal(payload, &m); err != nil {
			c.t.Fatalf("unmarshal: %v", err)
		}
		if m.T == proto.TErr {
			return m
		}
	}
	c.t.Fatalf("no error message received")
	return proto.Msg{}
}

// wantFrame reads until a frame of the given type arrives.
func (c *testClient) wantFrame(want byte) []byte {
	c.t.Helper()

	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		typ, payload := c.next()
		if typ == want {
			return payload
		}
	}
	c.t.Fatalf("timed out waiting for frame type %d", want)
	return nil
}

func (c *testClient) hello(nick, res string, protoVersion int) proto.Msg {
	c.t.Helper()

	c.send(proto.Msg{T: proto.THello, Ver: "0.2.0", Proto: protoVersion, Res: res, Nick: nick})
	return c.want(proto.TWelcome)
}

// startMatch drives both players through the picking phase.
func startMatch(t *testing.T, a, b *testClient) (proto.Msg, proto.Msg, []string, []string) {
	t.Helper()

	a.send(proto.Msg{T: proto.TStart})
	aPick := a.want(proto.TPickStart)
	bPick := b.want(proto.TPickStart)

	if aPick.Role != proto.RolePlant || bPick.Role != proto.RoleZombie {
		t.Fatalf("unexpected roles: %q / %q", aPick.Role, bPick.Role)
	}
	if aPick.Slots != 6 || bPick.Slots != 6 {
		t.Fatalf("unexpected slot count: %d / %d", aPick.Slots, bPick.Slots)
	}
	if aPick.Pool == nil || len(aPick.Pool.Plant) < 6 || len(aPick.Pool.Zombie) < 6 {
		t.Fatalf("pick pool is missing or too small")
	}
	if aPick.Deadline <= time.Now().UnixMilli() {
		t.Fatalf("pick deadline should be in the future")
	}
	// The board RNG seed is fixed when picking starts so both clients can build
	// the level before the final card list arrives.
	if aPick.Seed == 0 || bPick.Seed != aPick.Seed {
		t.Fatalf("pick_start seed mismatch: %d / %d", aPick.Seed, bPick.Seed)
	}

	plantCards := append([]string(nil), aPick.Pool.Plant[:6]...)
	zombieCards := append([]string(nil), bPick.Pool.Zombie[:6]...)

	a.send(proto.Msg{T: proto.TPick, Role: proto.RolePlant, Cards: plantCards})
	b.send(proto.Msg{T: proto.TPick, Role: proto.RoleZombie, Cards: zombieCards})

	aStart := a.want(proto.TStart)
	bStart := b.want(proto.TStart)
	if aStart.Seed != aPick.Seed || bStart.Seed != aPick.Seed {
		t.Fatalf("start seed %d/%d differs from pick_start seed %d", aStart.Seed, bStart.Seed, aPick.Seed)
	}

	return aStart, bStart, plantCards, zombieCards
}

func TestRoomLifecycle(t *testing.T) {
	addr := startServer(t, testRes)

	a := dial(t, addr)
	welcome := a.hello("玩家A", testRes, 1)
	if welcome.Pid != 1 {
		t.Fatalf("first player should get pid 1, got %d", welcome.Pid)
	}

	a.send(proto.Msg{T: proto.TCreate, Name: "老王的房间", Mode: "versus", Maxp: 2})
	room := a.want(proto.TRoom)
	if room.Code == "" || len(room.Code) != 6 {
		t.Fatalf("bad room code %q", room.Code)
	}
	if room.State != proto.StateIdle {
		t.Fatalf("new room should be idle, got %q", room.State)
	}

	b := dial(t, addr)
	b.hello("玩家B", testRes, 1)
	b.send(proto.Msg{T: proto.TJoin, Code: room.Code})

	// Both sides must see two members.
	bRoom := b.want(proto.TRoom)
	if len(bRoom.Members) != 2 {
		t.Fatalf("joiner sees %d members, want 2", len(bRoom.Members))
	}
	aRoom := a.want(proto.TRoom)
	if len(aRoom.Members) != 2 {
		t.Fatalf("host sees %d members, want 2", len(aRoom.Members))
	}

	yes := true
	a.send(proto.Msg{T: proto.TReady, Ready: &yes})
	b.send(proto.Msg{T: proto.TReady, Ready: &yes})

	state := a.wantRoomState(proto.StateReady)
	if state.State != proto.StateReady {
		t.Fatalf("room should be ready after both players, got %q", state.State)
	}

	aStart, bStart, plantCards, zombieCards := startMatch(t, a, b)
	if aStart.Seed == 0 {
		t.Fatalf("start seed must not be zero")
	}
	if aStart.Seed != bStart.Seed || aStart.StartAt != bStart.StartAt {
		t.Fatalf("players disagree on start: %+v vs %+v", aStart, bStart)
	}
	if aStart.StartAt <= time.Now().UnixMilli() {
		t.Fatalf("start time should be in the future")
	}
	if !reflect.DeepEqual(aStart.Picks.Plant, plantCards) {
		t.Fatalf("plant cards not echoed: %v vs %v", aStart.Picks.Plant, plantCards)
	}
	if !reflect.DeepEqual(aStart.Picks.Zombie, zombieCards) {
		t.Fatalf("zombie cards not echoed: %v vs %v", aStart.Picks.Zombie, zombieCards)
	}
	if !reflect.DeepEqual(aStart.Picks, bStart.Picks) {
		t.Fatalf("players got different picks")
	}

	// Match traffic must be relayed verbatim to the other side.
	payload := []byte{0x2a, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x10, 0x00, 0x20, 0x00}
	a.sendFrame(proto.TypeInput, payload)
	if got := b.wantFrame(proto.TypeInput); !bytes.Equal(got, payload) {
		t.Fatalf("relayed payload mismatch: %x vs %x", got, payload)
	}
}

func TestKick(t *testing.T) {
	addr := startServer(t, testRes)

	a := dial(t, addr)
	a.hello("房主", testRes, 1)
	a.send(proto.Msg{T: proto.TCreate, Name: "踢人测试", Maxp: 2})
	room := a.want(proto.TRoom)

	b := dial(t, addr)
	bWelcome := b.hello("捣乱的", testRes, 1)
	b.send(proto.Msg{T: proto.TJoin, Code: room.Code})
	b.want(proto.TRoom)
	a.want(proto.TRoom)

	a.send(proto.Msg{T: proto.TKick, Pid: bWelcome.Pid})
	left := b.want(proto.TLeft)
	if left.Reason != "kick" {
		t.Fatalf("expected kick reason, got %q", left.Reason)
	}

	after := a.want(proto.TRoom)
	if len(after.Members) != 1 {
		t.Fatalf("room should have 1 member after kick, got %d", len(after.Members))
	}
}

func TestHostMigration(t *testing.T) {
	addr := startServer(t, testRes)

	a := dial(t, addr)
	a.hello("房主", testRes, 1)
	a.send(proto.Msg{T: proto.TCreate, Name: "迁移测试", Maxp: 2})
	room := a.want(proto.TRoom)

	b := dial(t, addr)
	b.hello("接盘的", testRes, 1)
	b.send(proto.Msg{T: proto.TJoin, Code: room.Code})
	b.want(proto.TRoom)
	a.want(proto.TRoom)

	a.send(proto.Msg{T: proto.TLeave})

	// The remaining player only gets a room update: a player leaving an idle
	// room does not end the match.
	after := b.want(proto.TRoom)
	if len(after.Members) != 1 || after.Host != 2 {
		t.Fatalf("host should migrate to player 2, got host=%d members=%d", after.Host, len(after.Members))
	}
}

func TestVersionMismatchRejected(t *testing.T) {
	addr := startServer(t, testRes)

	c := dial(t, addr)
	c.send(proto.Msg{T: proto.THello, Ver: "9.9.9", Proto: 99, Res: testRes, Nick: "太新的版本"})

	m := c.wantErr()
	if m.Err != proto.ErrVersion {
		t.Fatalf("expected version error, got %d", m.Err)
	}

	// The server closes the connection after a version mismatch.
	if _, err := c.br.ReadByte(); err == nil {
		t.Fatalf("connection should be closed after version mismatch")
	}
}

func TestResourceMismatchRejected(t *testing.T) {
	addr := startServer(t, testRes)

	a := dial(t, addr)
	a.hello("玩家A", testRes, 1)
	a.send(proto.Msg{T: proto.TCreate, Name: "资源检查", Maxp: 2})
	room := a.want(proto.TRoom)

	b := dial(t, addr)
	b.hello("玩家B", "1073-cn-old", 1)
	b.send(proto.Msg{T: proto.TJoin, Code: room.Code})

	m := b.wantErr()
	if m.Err != proto.ErrResource {
		t.Fatalf("expected resource error, got %d", m.Err)
	}
}

func TestDisconnectDuringMatch(t *testing.T) {
	addr := startServer(t, testRes)

	a := dial(t, addr)
	a.hello("玩家A", testRes, 1)
	a.send(proto.Msg{T: proto.TCreate, Name: "掉线测试", Maxp: 2})
	room := a.want(proto.TRoom)

	b := dial(t, addr)
	b.hello("玩家B", testRes, 1)
	b.send(proto.Msg{T: proto.TJoin, Code: room.Code})
	b.want(proto.TRoom)
	a.want(proto.TRoom)

	yes := true
	a.send(proto.Msg{T: proto.TReady, Ready: &yes})
	b.send(proto.Msg{T: proto.TReady, Ready: &yes})
	a.wantRoomState(proto.StateReady)

	startMatch(t, a, b)

	_ = b.conn.Close()

	left := a.want(proto.TLeft)
	if left.Reason != "disconnect" {
		t.Fatalf("expected disconnect reason, got %q", left.Reason)
	}

	a.send(proto.Msg{T: proto.TRooms})
	list := a.want(proto.TRooms)
	if len(list.Rooms) != 0 {
		t.Fatalf("room should be dissolved after a disconnect, got %+v", list.Rooms)
	}
}

func TestRoomListAndLeave(t *testing.T) {
	addr := startServer(t, testRes)

	a := dial(t, addr)
	a.hello("玩家A", testRes, 1)
	a.send(proto.Msg{T: proto.TCreate, Name: "列表测试", Maxp: 2})
	room := a.want(proto.TRoom)

	a.send(proto.Msg{T: proto.TRooms})
	list := a.want(proto.TRooms)
	if len(list.Rooms) != 1 || list.Rooms[0].Code != room.Code {
		t.Fatalf("room list mismatch: %+v", list.Rooms)
	}
	if list.Rooms[0].N != 1 || list.Rooms[0].Host != "玩家A" {
		t.Fatalf("room list entry wrong: %+v", list.Rooms[0])
	}

	a.send(proto.Msg{T: proto.TLeave})
	a.want(proto.TLeft)

	a.send(proto.Msg{T: proto.TRooms})
	list = a.want(proto.TRooms)
	if len(list.Rooms) != 0 {
		t.Fatalf("room should be gone after the host left, got %+v", list.Rooms)
	}
}

// readyRoom returns two clients whose room is ready to start.
func readyRoom(t *testing.T, addr string) (*testClient, *testClient, string) {
	t.Helper()

	a := dial(t, addr)
	a.hello("玩家A", testRes, 1)
	a.send(proto.Msg{T: proto.TCreate, Name: "测试房", Mode: "versus", Maxp: 2})
	room := a.want(proto.TRoom)

	b := dial(t, addr)
	b.hello("玩家B", testRes, 1)
	b.send(proto.Msg{T: proto.TJoin, Code: room.Code})
	b.want(proto.TRoom)
	a.want(proto.TRoom)

	yes := true
	a.send(proto.Msg{T: proto.TReady, Ready: &yes})
	b.send(proto.Msg{T: proto.TReady, Ready: &yes})
	a.wantRoomState(proto.StateReady)

	return a, b, room.Code
}

func TestPickRejectsInvalidCards(t *testing.T) {
	addr := startServer(t, testRes)
	a, b, _ := readyRoom(t, addr)

	a.send(proto.Msg{T: proto.TStart})
	aPick := a.want(proto.TPickStart)
	bPick := b.want(proto.TPickStart)
	if len(aPick.Pool.Plant) < 6 || len(aPick.Pool.Zombie) < 6 {
		t.Fatalf("server pool too small")
	}

	pool := aPick.Pool.Plant
	cases := []struct {
		name string
		msg  proto.Msg
	}{
		{"少一张", proto.Msg{T: proto.TPick, Role: proto.RolePlant, Cards: pool[:5]}},
		{"重复", proto.Msg{T: proto.TPick, Role: proto.RolePlant,
			Cards: []string{pool[0], pool[0], pool[1], pool[2], pool[3], pool[4]}}},
		{"卡池之外", proto.Msg{T: proto.TPick, Role: proto.RolePlant,
			Cards: []string{pool[0], pool[1], pool[2], pool[3], pool[4], "SEED_IMITATER"}}},
		{"角色不符", proto.Msg{T: proto.TPick, Role: proto.RoleZombie, Cards: pool[:6]}},
	}

	for _, tc := range cases {
		a.send(tc.msg)
		m := a.wantErr()
		if m.Err != proto.ErrPick {
			t.Fatalf("%s: want error %d, got %d (%s)", tc.name, proto.ErrPick, m.Err, m.Text)
		}
	}

	// A legal pick must still work after the rejected ones.
	a.send(proto.Msg{T: proto.TPick, Role: proto.RolePlant, Cards: pool[:6]})
	b.send(proto.Msg{T: proto.TPick, Role: proto.RoleZombie, Cards: bPick.Pool.Zombie[:6]})
	a.want(proto.TStart)
	b.want(proto.TStart)
}

func TestPickTimeoutFillsRandomly(t *testing.T) {
	addr := startServerWith(t, lobby.Config{
		ProtoVersion:    1,
		ResourceVersion: testRes,
		PickTimeout:     200 * time.Millisecond,
	})
	a, b, _ := readyRoom(t, addr)

	a.send(proto.Msg{T: proto.TStart})
	aPick := a.want(proto.TPickStart)
	b.want(proto.TPickStart)

	aStart := a.want(proto.TStart)
	bStart := b.want(proto.TStart)
	if !reflect.DeepEqual(aStart.Picks, bStart.Picks) {
		t.Fatalf("timeout fill produced different picks")
	}

	checkFilled := func(name string, cards, pool []string) {
		t.Helper()
		if len(cards) != 6 {
			t.Fatalf("%s: want 6 cards, got %d (%v)", name, len(cards), cards)
		}
		seen := map[string]bool{}
		inPool := map[string]bool{}
		for _, c := range pool {
			inPool[c] = true
		}
		for _, c := range cards {
			if !inPool[c] {
				t.Fatalf("%s: %q is outside the pool", name, c)
			}
			if seen[c] {
				t.Fatalf("%s: duplicate card %q", name, c)
			}
			seen[c] = true
		}
	}

	checkFilled("plant", aStart.Picks.Plant, aPick.Pool.Plant)
	checkFilled("zombie", aStart.Picks.Zombie, aPick.Pool.Zombie)
}

func TestPickAfterStartRejected(t *testing.T) {
	addr := startServer(t, testRes)
	a, b, _ := readyRoom(t, addr)

	a.send(proto.Msg{T: proto.TStart})
	aPick := a.want(proto.TPickStart)
	b.want(proto.TPickStart)
	a.send(proto.Msg{T: proto.TPick, Role: proto.RolePlant, Cards: aPick.Pool.Plant[:6]})
	b.send(proto.Msg{T: proto.TPick, Role: proto.RoleZombie, Cards: aPick.Pool.Zombie[:6]})
	a.want(proto.TStart)
	b.want(proto.TStart)

	a.send(proto.Msg{T: proto.TPick, Role: proto.RolePlant, Cards: aPick.Pool.Plant[:6]})
	m := a.wantErr()
	if m.Err != proto.ErrInGame {
		t.Fatalf("want %d after the match started, got %d (%s)", proto.ErrInGame, m.Err, m.Text)
	}
}

func TestLeaveDuringPickingResetsRoom(t *testing.T) {
	addr := startServer(t, testRes)
	a, b, _ := readyRoom(t, addr)

	a.send(proto.Msg{T: proto.TStart})
	a.want(proto.TPickStart)
	b.want(proto.TPickStart)

	b.send(proto.Msg{T: proto.TLeave})
	if m := b.want(proto.TLeft); m.Reason != "leave" {
		t.Fatalf("unexpected leave reason %q", m.Reason)
	}

	room := a.wantRoomState(proto.StateIdle)
	if len(room.Members) != 1 {
		t.Fatalf("want 1 member after the guest left, got %d", len(room.Members))
	}
}
