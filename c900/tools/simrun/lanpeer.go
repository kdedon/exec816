// SPDX-License-Identifier: GPL-3.0-only

package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"hash/crc32"
	"io"
	"net"
	"os"
	"time"
)

// lanPeer is a host on the LANCE card's cable (-lanpeer): it answers ARP
// requests for its address and ICMP echo requests to it, both with
// checksums checked.  After it has answered the guest's first echo
// request it pings the guest back, ARP first, and sends echo request
// n+1 only after a correct reply to n, up to pingCount.  The card's
// cable also returns every frame the guest sends, as the Go model's
// loopback does.
type lanPeer struct {
	ip, guest net.IP
	mac       [6]byte
	guestMAC  [6]byte
	inject    func([]byte)
	pcap      io.Writer
	state     int // 0 idle, 1 ARP asked, 2 pinging, 3 done
	seq       uint16
	log       io.Writer
}

const pingCount = 3

var peerMAC = [6]byte{0x02, 0x00, 0x0a, 0x00, 0x09, 0x01}

func newLANPeer(ip string, inject func([]byte), pcapPath string) (*lanPeer, error) {
	p := &lanPeer{ip: net.ParseIP(ip).To4(), mac: peerMAC, inject: inject, log: os.Stderr}
	if p.ip == nil {
		return nil, fmt.Errorf("-lanpeer %q: want an IPv4 address", ip)
	}
	if pcapPath != "" {
		f, err := os.Create(pcapPath)
		if err != nil {
			return nil, err
		}
		hdr := []byte{0xd4, 0xc3, 0xb2, 0xa1, 2, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0xff, 0xff, 0, 0, 1, 0, 0, 0}
		f.Write(hdr)
		p.pcap = f
	}
	return p, nil
}

func (p *lanPeer) record(f []byte) {
	if p.pcap == nil {
		return
	}
	var h [16]byte
	t := time.Now()
	binary.LittleEndian.PutUint32(h[0:], uint32(t.Unix()))
	binary.LittleEndian.PutUint32(h[4:], uint32(t.Nanosecond()/1000))
	binary.LittleEndian.PutUint32(h[8:], uint32(len(f)))
	binary.LittleEndian.PutUint32(h[12:], uint32(len(f)))
	p.pcap.Write(h[:])
	p.pcap.Write(f)
}

func (p *lanPeer) send(f []byte) {
	for len(f) < 60 {
		f = append(f, 0)
	}
	p.record(f)
	p.inject(f)
}

func csum(b []byte) uint16 {
	var s uint32
	for i := 0; i+1 < len(b); i += 2 {
		s += uint32(b[i])<<8 | uint32(b[i+1])
	}
	if len(b)&1 != 0 {
		s += uint32(b[len(b)-1]) << 8
	}
	for s > 0xffff {
		s = s&0xffff + s>>16
	}
	return ^uint16(s)
}

func ether(dst, src [6]byte, typ uint16) []byte {
	f := append(append([]byte{}, dst[:]...), src[:]...)
	return binary.BigEndian.AppendUint16(f, typ)
}

func (p *lanPeer) arp(op uint16, tha [6]byte, tpa net.IP) {
	dst := tha
	if op == 1 {
		dst = [6]byte{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}
		tha = [6]byte{}
	}
	f := ether(dst, p.mac, 0x0806)
	f = append(f, 0, 1, 8, 0, 6, 4, 0, byte(op))
	f = append(append(append(append(f, p.mac[:]...), p.ip...), tha[:]...), tpa...)
	p.send(f)
}

// ip4 builds an IPv4 ICMP datagram from the peer to dst.
func (p *lanPeer) icmp(dstMAC [6]byte, dst net.IP, typ byte, id, seq uint16, data []byte) {
	ic := []byte{typ, 0, 0, 0}
	ic = binary.BigEndian.AppendUint16(ic, id)
	ic = binary.BigEndian.AppendUint16(ic, seq)
	ic = append(ic, data...)
	binary.BigEndian.PutUint16(ic[2:], csum(ic))
	ip := []byte{0x45, 0, 0, 0, 0, 0, 0, 0, 64, 1, 0, 0}
	binary.BigEndian.PutUint16(ip[2:], uint16(20+len(ic)))
	ip = append(append(ip, p.ip...), dst...)
	binary.BigEndian.PutUint16(ip[10:], csum(ip))
	p.send(append(append(ether(dstMAC, p.mac, 0x0800), ip...), ic...))
}

func pingData(seq uint16) []byte {
	d := make([]byte, 32)
	for i := range d {
		d[i] = byte(int(seq)*7 + i)
	}
	return d
}

func (p *lanPeer) ping() {
	p.seq++
	fmt.Fprintf(p.log, "lanpeer: echo request %d to %v\n", p.seq, p.guest)
	p.icmp(p.guestMAC, p.guest, 8, 0x900, p.seq, pingData(p.seq))
}

// transmit sees every frame the guest puts on the cable.
func (p *lanPeer) transmit(f []byte) {
	if n := len(f); n >= 4 && crc32.ChecksumIEEE(f[:n-4]) == binary.LittleEndian.Uint32(f[n-4:]) {
		f = f[:n-4]
	}
	p.record(f)
	if len(f) < 14 {
		return
	}
	var dst, src [6]byte
	copy(dst[:], f[0:6])
	copy(src[:], f[6:12])
	typ := binary.BigEndian.Uint16(f[12:])
	b := f[14:]
	switch {
	case typ == 0x0806 && len(b) >= 28 && bytes.Equal(b[0:8], []byte{0, 1, 8, 0, 6, 4, 0, b[7]}):
		var sha [6]byte
		copy(sha[:], b[8:14])
		spa := net.IP(append([]byte{}, b[14:18]...))
		tpa := net.IP(b[24:28])
		switch {
		case b[7] == 1 && tpa.Equal(p.ip):
			fmt.Fprintf(p.log, "lanpeer: ARP request from %v\n", spa)
			p.arp(2, sha, spa)
		case b[7] == 2 && p.state == 1 && spa.Equal(p.guest) && dst == p.mac:
			fmt.Fprintf(p.log, "lanpeer: ARP reply from %v\n", spa)
			p.guestMAC = sha
			p.state = 2
			p.ping()
		}
	case typ == 0x0800 && dst == p.mac && len(b) >= 28 && b[0] == 0x45 && b[9] == 1:
		n := int(binary.BigEndian.Uint16(b[2:]))
		if n < 28 || n > len(b) || csum(b[:20]) != 0 || csum(b[20:n]) != 0 ||
			!net.IP(b[16:20]).Equal(p.ip) {
			fmt.Fprintf(p.log, "lanpeer: bad IP/ICMP datagram\n")
			return
		}
		ic := b[20:n]
		from := net.IP(append([]byte{}, b[12:16]...))
		id := binary.BigEndian.Uint16(ic[4:])
		seq := binary.BigEndian.Uint16(ic[6:])
		switch ic[0] {
		case 8:
			fmt.Fprintf(p.log, "lanpeer: echo request %d from %v\n", seq, from)
			p.icmp(src, from, 0, id, seq, ic[8:])
			if p.state == 0 {
				p.guest = from
				p.state = 1
				p.arp(1, [6]byte{}, from)
			}
		case 0:
			if p.state != 2 || id != 0x900 || seq != p.seq || !bytes.Equal(ic[8:], pingData(seq)) {
				fmt.Fprintf(p.log, "lanpeer: unexpected echo reply %d\n", seq)
				return
			}
			fmt.Fprintf(p.log, "lanpeer: echo reply %d\n", seq)
			if p.seq < pingCount {
				p.ping()
			} else {
				p.state = 3
			}
		}
	}
}
