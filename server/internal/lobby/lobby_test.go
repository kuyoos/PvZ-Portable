// SPDX-License-Identifier: LGPL-3.0-or-later
package lobby_test

import (
	"bufio"
	"bytes"
	"encoding/json"
	"net"
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

	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}

	hub := lobby.New(lobby.Config{ProtoVersion: 1, ResourceVersion: res})
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

	a.send(proto.Msg{T: proto.TStart})
	aStart := a.want(proto.TStart)
	bStart := b.want(proto.TStart)
	if aStart.Seed == 0 {
		t.Fatalf("start seed must not be zero")
	}
	if aStart.Seed != bStart.Seed || aStart.StartAt != bStart.StartAt {
		t.Fatalf("players disagree on start: %+v vs %+v", aStart, bStart)
	}
	if aStart.StartAt <= time.Now().UnixMilli() {
		t.Fatalf("start time should be in the future")
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

	a.send(proto.Msg{T: proto.TStart})
	a.want(proto.TStart)
	b.want(proto.TStart)

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
