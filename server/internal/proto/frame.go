// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Wire format shared by the lobby server and the game client.
//
//	+---------+--------+------------------+
//	| len u32 | type u8| payload (len-1)  |
//	+---------+--------+------------------+
//
// len is little endian and counts the type byte. Payloads are capped so a
// malicious client cannot make the server allocate unbounded memory.
package proto

import (
	"bufio"
	"encoding/binary"
	"fmt"
	"io"
)

const (
	TypeJSON  byte = 1
	TypeInput byte = 2
	TypePing  byte = 3
	TypePong  byte = 4
	TypeState byte = 5
	TypeHash  byte = 6
)

// MaxFrame is the largest frame the server accepts or emits (64 KiB).
const MaxFrame = 64 * 1024

// WriteFrame writes one frame. Callers must guarantee a single writer per
// connection; every Client owns exactly one writer goroutine.
func WriteFrame(w io.Writer, typ byte, payload []byte) error {
	total := len(payload) + 1
	if total > MaxFrame {
		return fmt.Errorf("proto: frame too large (%d bytes)", total)
	}

	var head [5]byte
	binary.LittleEndian.PutUint32(head[:4], uint32(total))
	head[4] = typ

	if _, err := w.Write(head[:]); err != nil {
		return err
	}
	if len(payload) == 0 {
		return nil
	}
	_, err := w.Write(payload)
	return err
}

// ReadFrame reads one frame, returning its type and payload.
func ReadFrame(r *bufio.Reader) (byte, []byte, error) {
	var head [5]byte
	if _, err := io.ReadFull(r, head[:]); err != nil {
		return 0, nil, err
	}

	total := binary.LittleEndian.Uint32(head[:4])
	if total == 0 || total > MaxFrame {
		return 0, nil, fmt.Errorf("proto: bad frame length %d", total)
	}

	payload := make([]byte, total-1)
	if _, err := io.ReadFull(r, payload); err != nil {
		return 0, nil, err
	}
	return head[4], payload, nil
}
