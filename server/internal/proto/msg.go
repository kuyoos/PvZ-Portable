// SPDX-License-Identifier: LGPL-3.0-or-later
//
// JSON control messages carried in TypeJSON frames. One struct covers both
// directions; unused fields stay empty thanks to omitempty. Keeping a single
// struct makes the wire format easy to extend without breaking old clients.
package proto

// Error codes reported in Msg.Err.
const (
	ErrVersion     = 1001 // client/protocol version mismatch
	ErrResource    = 1002 // resource version mismatch
	ErrNoRoom      = 1003 // room missing or closed
	ErrRoomFull    = 1004
	ErrInGame      = 1005 // already inside a running match
	ErrNotHost     = 1006
	ErrBadRequest  = 1007 // malformed nickname/room name/etc.
	ErrTimeout     = 1008
	ErrNotReady    = 1009 // start requested while someone is not ready
	ErrTooFew      = 1010 // start requested with fewer than two players
	ErrRateLimited = 1011
	ErrPick        = 1012 // illegal card selection
)

// Message types (Msg.T).
const (
	THello     = "hello"
	TWelcome   = "welcome"
	TRooms     = "rooms"
	TCreate    = "create"
	TJoin      = "join"
	TLeave     = "leave"
	TKick      = "kick"
	TReady     = "ready"
	TStart     = "start"
	TRoom      = "room"
	TLeft      = "left"
	TErr       = "err"
	TChat      = "chat"
	TPrtt      = "prtt" // client reports its measured round trip time
	TPick      = "pick" // client submits its six cards
	TPickStart = "pick_start"
	TPickState = "pick_state"
)

// Room states.
const (
	StateIdle    = "idle"
	StateReady   = "ready"
	StatePicking = "picking"
	StatePlaying = "playing"
	StateClosed  = "closed"
)

// Roles a player can take in a versus room.
const (
	RolePlant  = "plant"
	RoleZombie = "zombie"

	// What the host asks for when creating a room.
	HostRolePlant  = "host_plant"
	HostRoleZombie = "host_zombie"
	HostRoleRandom = "random"
)

// Pools lists the card names each side may pick from.
type Pools struct {
	Plant  []string `json:"plant"`
	Zombie []string `json:"zombie"`
}

// Picks is the final six card list of both sides.
type Picks struct {
	Plant  []string `json:"plant"`
	Zombie []string `json:"zombie"`
}

// Msg is one control message. Field names are short on the wire on purpose.
type Msg struct {
	T string `json:"t"`

	// handshake
	Ver        string `json:"ver,omitempty"`
	Proto      int    `json:"proto,omitempty"`
	Res        string `json:"res,omitempty"`
	Nick       string `json:"nick,omitempty"`
	Pid        int    `json:"pid,omitempty"`
	ServerTime int64  `json:"serverTime,omitempty"`

	// room
	Code    string       `json:"code,omitempty"`
	Name    string       `json:"name,omitempty"`
	Mode    string       `json:"mode,omitempty"`
	Maxp    int          `json:"maxp,omitempty"`
	Ready   *bool        `json:"ready,omitempty"`
	State   string       `json:"state,omitempty"`
	Host    int          `json:"host,omitempty"`
	Seed    uint32       `json:"seed,omitempty"`
	StartAt int64        `json:"startAt,omitempty"`
	Rooms   []RoomInfo   `json:"list,omitempty"`
	Members []MemberInfo `json:"members,omitempty"`

	// notices
	Err    int    `json:"err,omitempty"`
	Reason string `json:"reason,omitempty"`
	Text   string `json:"text,omitempty"`

	// card picking (see 5.7 in the design doc)
	Role     string   `json:"role,omitempty"`
	Pool     *Pools   `json:"pool,omitempty"`
	Slots    int      `json:"slots,omitempty"`
	Deadline int64    `json:"deadline,omitempty"`
	Cards    []string `json:"cards,omitempty"`
	Picks    *Picks   `json:"picks,omitempty"`
	Done     []int    `json:"done,omitempty"`

	// latency probe
	Cseq uint32 `json:"cseq,omitempty"`
	T0   int64  `json:"t0,omitempty"`
	Ping int    `json:"ping,omitempty"`
}

// RoomInfo is one entry of the room list.
type RoomInfo struct {
	Code  string `json:"code"`
	Name  string `json:"name"`
	Host  string `json:"host"`
	Mode  string `json:"mode"`
	N     int    `json:"n"`
	Maxp  int    `json:"maxp"`
	State string `json:"state"`
	Res   string `json:"res"`
}

// MemberInfo describes a player inside a room.
type MemberInfo struct {
	Pid   int    `json:"pid"`
	Nick  string `json:"nick"`
	Ready bool   `json:"ready"`
	Host  bool   `json:"host"`
	Ping  int    `json:"ping"`
	Res   string `json:"res,omitempty"`
}
