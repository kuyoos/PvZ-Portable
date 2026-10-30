// SPDX-License-Identifier: LGPL-3.0-or-later
package lobby

import (
	"crypto/rand"
	"errors"
	"math/big"

	"pvzlobby/internal/proto"
)

// pickSlots is how many cards each side brings into a match.
const pickSlots = 6

// Cards are identified by their SeedType name so the Go server never has to
// duplicate the C++ enum numbering. Names match src/ConstEnums.h.
var plantPool = []string{
	"SEED_SUNFLOWER", "SEED_PEASHOOTER", "SEED_REPEATER", "SEED_SNOWPEA",
	"SEED_WALLNUT", "SEED_TALLNUT", "SEED_POTATOMINE", "SEED_CHERRYBOMB",
	"SEED_CHOMPER", "SEED_THREEPEATER", "SEED_MELONPULT", "SEED_WINTERMELON",
	"SEED_FUMESHROOM", "SEED_MAGNETSHROOM", "SEED_SQUASH", "SEED_GARLIC",
	"SEED_HYPNOSHROOM", "SEED_SPIKEWEED",
}

var zombiePool = []string{
	"SEED_ZOMBIE_NORMAL", "SEED_ZOMBIE_TRAFFIC_CONE", "SEED_ZOMBIE_POLEVAULTER",
	"SEED_ZOMBIE_SCREEN_DOOR", "SEED_ZOMBIE_PAIL", "SEED_ZOMBIE_DIGGER",
	"SEED_ZOMBIE_LADDER", "SEED_ZOMBIE_BALLOON", "SEED_ZOMBIE_FOOTBALL",
	"SEED_ZOMBIE_POGO", "SEED_ZOMBIE_GARGANTUAR", "SEED_ZOMBIE_DANCER",
}

var (
	errPickCount   = errors.New("卡组数量必须是 6 张")
	errPickDup     = errors.New("卡组里有重复的卡")
	errPickOutside = errors.New("卡组里有卡池之外的卡")
)

func pools() *proto.Pools {
	return &proto.Pools{Plant: plantPool, Zombie: zombiePool}
}

func poolFor(role string) []string {
	if role == proto.RoleZombie {
		return zombiePool
	}
	return plantPool
}

// otherRole returns the role the joining player has to take: the opposite of
// whoever is already in the room.
func otherRole(r *Room) string {
	for _, pid := range r.Order {
		if role, ok := r.Role[pid]; ok {
			if role == proto.RoleZombie {
				return proto.RolePlant
			}
			return proto.RoleZombie
		}
	}
	return proto.RolePlant
}

func randomRole() string {
	n, err := rand.Int(rand.Reader, big.NewInt(2))
	if err == nil && n.Int64() == 1 {
		return proto.RoleZombie
	}
	return proto.RolePlant
}

func validateCards(cards []string, pool []string) error {
	if len(cards) != pickSlots {
		return errPickCount
	}

	seen := make(map[string]bool, pickSlots)
	inPool := make(map[string]bool, len(pool))
	for _, name := range pool {
		inPool[name] = true
	}

	for _, name := range cards {
		if seen[name] {
			return errPickDup
		}
		seen[name] = true
		if !inPool[name] {
			return errPickOutside
		}
	}
	return nil
}

// fillPicks completes a possibly empty selection to six distinct cards.
// The result is always shipped in the start message, so the client never has
// to reproduce this randomness.
func fillPicks(cards []string, pool []string) []string {
	out := make([]string, 0, pickSlots)
	taken := make(map[string]bool, pickSlots)
	for _, name := range cards {
		if len(out) == pickSlots || taken[name] {
			continue
		}
		taken[name] = true
		out = append(out, name)
	}

	candidates := make([]string, 0, len(pool))
	for _, name := range pool {
		if !taken[name] {
			candidates = append(candidates, name)
		}
	}

	for len(out) < pickSlots && len(candidates) > 0 {
		idx, err := rand.Int(rand.Reader, big.NewInt(int64(len(candidates))))
		if err != nil {
			idx = big.NewInt(0)
		}
		pick := int(idx.Int64())
		out = append(out, candidates[pick])
		candidates = append(candidates[:pick], candidates[pick+1:]...)
	}
	return out
}
