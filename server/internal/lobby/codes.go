// SPDX-License-Identifier: LGPL-3.0-or-later
package lobby

import (
	"crypto/rand"
	"math/big"
)

// codeAlphabet leaves out I, O, 0 and 1 so codes can be read aloud or typed
// from a phone screen without ambiguity.
const codeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"

const codeLength = 6

// newRoomCode returns a code that is not yet taken.
func newRoomCode(taken func(string) bool) (string, bool) {
	for attempt := 0; attempt < 32; attempt++ {
		buf := make([]byte, codeLength)
		for i := range buf {
			n, err := rand.Int(rand.Reader, big.NewInt(int64(len(codeAlphabet))))
			if err != nil {
				return "", false
			}
			buf[i] = codeAlphabet[n.Int64()]
		}
		code := string(buf)
		if !taken(code) {
			return code, true
		}
	}
	return "", false
}
