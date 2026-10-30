// SPDX-License-Identifier: LGPL-3.0-or-later
package lobby

import (
	"time"

	"pvzlobby/internal/proto"
)

// Room holds the server authoritative state of one lobby room. Every method
// here is called with Hub.mu held, so no field needs its own lock.
type Room struct {
	Code  string
	Name  string
	Host  int
	Mode  string
	MaxP  int
	State string
	Seed  uint32
	Res   string

	Order []int // pids in join order; Order[0] is normally the host
	Ready map[int]bool
	Ping  map[int]int

	// Card picking (design doc 5.7).
	Role         map[int]string   // pid -> proto.RolePlant / proto.RoleZombie
	Picks        map[int][]string // pid -> submitted cards
	PickDeadline time.Time
	PickTimer    *time.Timer
}

func newRoom(code, name string, host int, mode string, maxP int, res, hostRole string) *Room {
	return &Room{
		Code:  code,
		Name:  name,
		Host:  host,
		Mode:  mode,
		MaxP:  maxP,
		State: proto.StateIdle,
		Res:   res,
		Order: []int{host},
		Ready: map[int]bool{},
		Ping:  map[int]int{},
		Role:  map[int]string{host: hostRole},
		Picks: map[int][]string{},
	}
}

func (r *Room) has(pid int) bool {
	for _, p := range r.Order {
		if p == pid {
			return true
		}
	}
	return false
}

func (r *Room) remove(pid int) {
	for i, p := range r.Order {
		if p == pid {
			r.Order = append(r.Order[:i], r.Order[i+1:]...)
			break
		}
	}
	delete(r.Ready, pid)
	delete(r.Ping, pid)
	delete(r.Role, pid)
	delete(r.Picks, pid)

	if r.Host == pid && len(r.Order) > 0 {
		r.Host = r.Order[0]
	}
}

func (r *Room) allReady() bool {
	if len(r.Order) < 2 {
		return false
	}
	for _, p := range r.Order {
		if !r.Ready[p] {
			return false
		}
	}
	return true
}

// refreshState moves the room between idle and ready. It never leaves the
// playing state: once a match starts the room only closes.
func (r *Room) refreshState() {
	if r.State == proto.StatePlaying {
		return
	}
	if r.allReady() {
		r.State = proto.StateReady
	} else {
		r.State = proto.StateIdle
	}
}
