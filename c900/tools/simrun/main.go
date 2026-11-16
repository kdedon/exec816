// SPDX-License-Identifier: GPL-3.0-only

// Command simrun boots a C900 hard-disk image in the C900 Go machine model
// (github.com C900/sim, package c900/machine) without its debugger UI or
// HTTP server, and stops when the guest writes the emulator stop doorbell.
//
//	simrun -hd IMAGE [-fd IMAGE] [-board none|lr|hr] [-max TICKS] [-log FILE]
//	       [-screen FILE] [-key TICK:CODE,...] [-mouse TICK:DX:DY[:LRM],...]
//	       [-rtc TIME] [-ramcard none|3M|16M] [-shortcut hdc,fdc,kbd]
//	       [-hrjp3 open|4PC|M/C|M/C+4PC] [-marks]
//	       [-profile FILE [-profrange FROM:TO]]
//	       [-lan [-lanmac MAC] [-lanpeer IP] [-lanlog FILE]]
//
// The console (SCC U74 channel B) goes to stdout, or to -log.  The image is
// opened read-only behind a copy-on-write cache.  With -marks every I/O
// write to port 0x0F00 + 2N (N < 126) prints "mark N TICK" on stderr, for
// timing a guest's phases; -profile samples the PC between two marks.
// -lan fits the Am7990 LANCE card, whose cable returns every frame the
// guest sends; -lanpeer adds a host there that answers ARP and ICMP echo
// and pings the guest back (lanpeer.go); -lanlog writes the peer's view
// of the cable as pcap.  Exit status: 0 stopped on
// the doorbell, 3 ran out of -max ticks, 4 the console was quiet for -idle
// ticks, 1 any other error.
package main

import (
	"bufio"
	"flag"
	"fmt"
	"image"
	"image/png"
	"io"
	"os"
	"sort"
	"strconv"
	"strings"
	"time"

	"c900/chips/fdc"
	"c900/chips/hrdisp"
	"c900/chips/ramcard"
	"c900/chips/wd2010"
	"c900/machine"
	"z8000/zbus"
)

type event struct {
	at      uint64
	key     int // -1: mouse event
	dx, dy  int
	l, r, m bool
}

func parseEvents(keys, mouse string) ([]event, error) {
	var evs []event
	for _, f := range splitList(keys) {
		at, code, ok := strings.Cut(f, ":")
		if !ok {
			return nil, fmt.Errorf("-key %q: want TICK:CODE", f)
		}
		t, err := strconv.ParseUint(at, 0, 64)
		if err != nil {
			return nil, fmt.Errorf("-key %q: %v", f, err)
		}
		c, err := strconv.ParseUint(code, 0, 8)
		if err != nil {
			return nil, fmt.Errorf("-key %q: %v", f, err)
		}
		evs = append(evs, event{at: t, key: int(c)})
	}
	for _, f := range splitList(mouse) {
		p := strings.Split(f, ":")
		if len(p) < 3 || len(p) > 4 {
			return nil, fmt.Errorf("-mouse %q: want TICK:DX:DY[:LRM]", f)
		}
		t, err := strconv.ParseUint(p[0], 0, 64)
		if err != nil {
			return nil, fmt.Errorf("-mouse %q: %v", f, err)
		}
		dx, err1 := strconv.Atoi(p[1])
		dy, err2 := strconv.Atoi(p[2])
		if err1 != nil || err2 != nil {
			return nil, fmt.Errorf("-mouse %q: bad motion", f)
		}
		e := event{at: t, key: -1, dx: dx, dy: dy}
		if len(p) == 4 {
			e.l = strings.ContainsRune(p[3], 'L')
			e.r = strings.ContainsRune(p[3], 'R')
			e.m = strings.ContainsRune(p[3], 'M')
		}
		evs = append(evs, e)
	}
	sort.SliceStable(evs, func(i, j int) bool { return evs[i].at < evs[j].at })
	return evs, nil
}

func splitList(s string) []string {
	if s == "" {
		return nil
	}
	return strings.Split(s, ",")
}

// writeScreen saves the display: LR as text (80x25) plus a PNG of the
// rendered frame next to it, HR as a PBM bitmap.
func writeScreen(m *machine.C900, path string) error {
	switch {
	case m.LRDisp != nil:
		var b strings.Builder
		for row := 0; row < 25; row++ {
			line := make([]byte, 80)
			for col := 0; col < 80; col++ {
				c, _ := m.LRDisp.CharAt(col, row)
				c &= 0x7F
				if c < 0x20 || c == 0x7F {
					c = ' '
				}
				line[col] = c
			}
			b.WriteString(strings.TrimRight(string(line), " "))
			b.WriteByte('\n')
		}
		if err := os.WriteFile(path, []byte(b.String()), 0o644); err != nil {
			return err
		}
		m.LRDisp.Render()
		const w, h = 720, 350
		img := image.NewRGBA(image.Rect(0, 0, w, h))
		copy(img.Pix, m.LRDisp.Framebuffer)
		f, err := os.Create(strings.TrimSuffix(path, ".txt") + ".png")
		if err != nil {
			return err
		}
		defer f.Close()
		return png.Encode(f, img)
	case m.HRDisp != nil:
		w, h := m.HRDisp.ActiveWidth(), m.HRDisp.ActiveHeight()
		f, err := os.Create(path)
		if err != nil {
			return err
		}
		bw := bufio.NewWriter(f)
		fmt.Fprintf(bw, "P4\n%d %d\n", w, h)
		for y := 0; y < h; y++ {
			for x := 0; x < w/8; x++ {
				bw.WriteByte(m.HRDisp.RawPixelByte(y, x))
			}
		}
		if err := bw.Flush(); err != nil {
			f.Close()
			return err
		}
		if err := f.Close(); err != nil {
			return err
		}
		return os.WriteFile(strings.TrimSuffix(path, ".pbm")+".txt", []byte(hrText(m)), 0o644)
	}
	return fmt.Errorf("no display card fitted")
}

// hrText reads the HR bitmap as 64x50 cells of the boot ROM's 16x16 font,
// the face the ROM paints its own screen with; '?' marks a cell that is
// not one of its glyphs.
func hrText(m *machine.C900) string {
	const base, stride, payload = 0x45FE, 0x42, 10
	font := map[[32]byte]byte{}
	for ch := 0x21; ch < 0x7F; ch++ {
		var g [32]byte
		for i := range g {
			g[i] = m.BIOSReadByte(uint32(base + stride*ch + payload + i))
		}
		if _, dup := font[g]; !dup {
			font[g] = byte(ch)
		}
	}
	var b strings.Builder
	for row := 0; row < 50; row++ {
		line := make([]byte, 64)
		for col := range line {
			g := m.HRCellBytes(col, row)
			c, ok := font[g]
			switch {
			case g == [32]byte{}:
				line[col] = ' '
			case ok:
				line[col] = c
			default:
				line[col] = '?'
			}
		}
		s := strings.TrimRight(string(line), " ")
		if strings.Trim(s, "? ") == "" && s != "" {
			continue // lower halves of the ROM's 32-line text rows
		}
		b.WriteString(s)
		b.WriteByte('\n')
	}
	return b.String()
}

// writeProfile saves the PC samples, most frequent first.
func writeProfile(path string, hist map[uint32]uint64) error {
	pcs := make([]uint32, 0, len(hist))
	for pc := range hist {
		pcs = append(pcs, pc)
	}
	sort.Slice(pcs, func(i, j int) bool { return hist[pcs[i]] > hist[pcs[j]] })
	var b strings.Builder
	for _, pc := range pcs {
		fmt.Fprintf(&b, "%06X %d\n", pc, hist[pc])
	}
	return os.WriteFile(path, []byte(b.String()), 0o644)
}

type inByte struct {
	b    byte
	held bool
}

func parseInput(s string) []inByte {
	var out []inByte
	held := false
	for i := 0; i < len(s); i++ {
		c := s[i]
		if c == '\\' && i+1 < len(s) {
			i++
			switch s[i] {
			case 'i':
				held = true
				continue
			case 'r':
				c = '\r'
			case 'n':
				c = '\n'
			case 't':
				c = '\t'
			default:
				c = s[i]
			}
		}
		out = append(out, inByte{c, held})
		held = false
	}
	return out
}

func why(stopped, quiet bool, idle uint64) string {
	switch {
	case stopped:
		return "stopped on doorbell"
	case quiet:
		return fmt.Sprintf("no console byte for %d ticks", idle)
	}
	return "tick budget exhausted"
}

func main() {
	hd := flag.String("hd", "", "hard disk image (opened read-only)")
	fd := flag.String("fd", "", "floppy image for drive 0")
	board := flag.String("board", "none", "display card: none, lr or hr")
	fw := flag.String("firmware", "", "ROM directory (default ~/git/C900/firmware)")
	maxTicks := flag.Uint64("max", 2_000_000_000, "tick budget")
	stopPort := flag.Uint("stop-port", 0x0FFE, "I/O port whose write ends the run")
	logPath := flag.String("log", "", "console transcript (default stdout)")
	screen := flag.String("screen", "", "save the display at the end: text+PNG (lr), PBM (hr)")
	keys := flag.String("key", "", "XT scancodes injected at ticks: TICK:CODE,...")
	mouse := flag.String("mouse", "", "mouse motion/buttons at ticks (hr): TICK:DX:DY[:LRM],...")
	memtest := flag.Bool("memtest", false, "run the BIOS memory test in full")
	idle := flag.Uint64("idle", 150_000_000, "give up after this many ticks without a console byte (0: never)")
	stamps := flag.Bool("stamps", false, "also print each console line to stderr with its tick")
	rtc := flag.String("rtc", "2026-01-01 00:00:00", "RTC start time, YYYY-MM-DD HH:MM:SS")
	ramCard := flag.String("ramcard", "none", "RAM expansion card: none, 3M or 16M")
	input := flag.String("input", "", `console input (\r \n \t \\ escapes; \i holds the next byte until -input-mark is printed), one byte whenever the receiver is idle`)
	inputMark := flag.String("input-mark", "", "console text that releases the \\i bytes of -input")
	shortcuts := flag.String("shortcut", "", "functional bypasses of the sub-CPUs: any of hdc,fdc,kbd")
	printer := flag.String("printer", "", "attach a printer on CN5 that appends every byte to this file")
	wav := flag.String("wav", "", "capture the speaker drive to this WAV file")
	hrjp3 := flag.String("hrjp3", "open", "HR JP3 links: open (1024x800), 4PC (1024x1024), M/C or M/C+4PC (640x400 colour)")
	profile := flag.String("profile", "", `write a PC histogram ("SEGOFF COUNT", a sample every 16 ticks) to this file`)
	profRange := flag.String("profrange", "1:99", "profile between marks FROM:TO (with -profile)")
	lan := flag.Bool("lan", false, "fit the Am7990 LANCE Ethernet card")
	lanMAC := flag.String("lanmac", "", "the LANCE card's station address PROM (default the model's)")
	lanPeerIP := flag.String("lanpeer", "", "with -lan: a host at this IPv4 address on the cable")
	lanLog := flag.String("lanlog", "", "with -lanpeer: every frame on the cable, pcap")
	marks := flag.Bool("marks", false, `print "mark N TICK" on stderr for each I/O write to 0x0F00+2N`)
	flag.Parse()
	if *hd == "" {
		fmt.Fprintln(os.Stderr, "simrun: -hd is required")
		os.Exit(2)
	}
	evs, err := parseEvents(*keys, *mouse)
	if err != nil {
		fmt.Fprintln(os.Stderr, "simrun:", err)
		os.Exit(2)
	}
	var kind machine.DisplayBoard
	switch *board {
	case "none", "headless":
		kind = machine.Headless
	case "lr":
		kind = machine.LR
	case "hr":
		kind = machine.HR
	default:
		fmt.Fprintf(os.Stderr, "simrun: -board %q: want none, lr or hr\n", *board)
		os.Exit(2)
	}

	var out io.Writer = os.Stdout
	if *logPath != "" {
		f, err := os.Create(*logPath)
		if err != nil {
			fmt.Fprintln(os.Stderr, "simrun:", err)
			os.Exit(1)
		}
		defer f.Close()
		out = f
	}
	console := bufio.NewWriter(out)
	defer console.Flush()

	m, err := machine.New(machine.Config{
		DisplayBoard:    kind,
		FirmwareDir:     *fw,
		BIOSSkipMemTest: !*memtest,
		ShortcutHDC:     strings.Contains(*shortcuts, "hdc"),
		ShortcutFDC:     strings.Contains(*shortcuts, "fdc"),
		ShortcutKBD:     strings.Contains(*shortcuts, "kbd"),
		RTCStartTime:    *rtc,
		RAMCard:         ramcard.ParseSize(*ramCard),
		HRJP3:           hrdisp.ParseJP3(*hrjp3),
		LANBoard:        *lan,
		LANMAC:          *lanMAC,
	})
	if err != nil {
		fmt.Fprintln(os.Stderr, "simrun: machine:", err)
		os.Exit(1)
	}
	if *printer != "" || *wav != "" {
		if err := m.Opts.Set("printer.file", *printer); err != nil {
			fmt.Fprintln(os.Stderr, "simrun: printer:", err)
			os.Exit(2)
		}
		if err := m.Opts.Set("audio.wav_file", *wav); err != nil {
			fmt.Fprintln(os.Stderr, "simrun: wav:", err)
			os.Exit(2)
		}
		m.HardReset()
	}
	if *lanPeerIP != "" && m.LANBoard != nil {
		loop := m.LANBoard.Loop
		peer, err := newLANPeer(*lanPeerIP, loop.InjectFrame, *lanLog)
		if err != nil {
			fmt.Fprintln(os.Stderr, "simrun:", err)
			os.Exit(2)
		}
		loop.OnTransmit = peer.transmit
	}
	res, err := wd2010.Open(*hd, false)
	if err != nil {
		fmt.Fprintln(os.Stderr, "simrun:", err)
		os.Exit(1)
	}
	persist := false
	m.InsertHD(0, wd2010.NewCOWImage(res.Image, &persist))
	var n, lastByte uint64
	var line []byte
	feed := parseInput(*input)
	var tail []byte
	markSeen := *inputMark == ""
	if *fd != "" {
		img, err := fdc.LoadFromFile(*fd, true)
		if err != nil {
			fmt.Fprintln(os.Stderr, "simrun:", err)
			os.Exit(1)
		}
		m.InsertDisk(0, img)
	}
	m.SCC.TXCallback = func(ch int, b byte) {
		if ch != 1 {
			return
		}
		console.WriteByte(b)
		lastByte = n
		if !markSeen {
			tail = append(tail, b)
			if len(tail) > 4096 {
				tail = tail[len(tail)-2048:]
			}
			markSeen = strings.Contains(string(tail), *inputMark)
		}
		if !*stamps {
			return
		}
		if b == '\n' {
			fmt.Fprintf(os.Stderr, "%11d %s\n", n, strings.TrimRight(string(line), "\r"))
			line = line[:0]
		} else {
			line = append(line, b)
		}
	}

	port := uint16(*stopPort)
	hist := map[uint32]uint64{}
	profFrom, profTo := -1, -1
	if *profile != "" {
		if _, err := fmt.Sscanf(*profRange, "%d:%d", &profFrom, &profTo); err != nil {
			fmt.Fprintln(os.Stderr, "simrun: -profrange: want FROM:TO")
			os.Exit(2)
		}
		*marks = true
	}
	profOn := false
	start := time.Now()
	stopped, quiet := false, false
	prevAS := true
	ei := 0
	for n = 0; n < *maxTicks; n++ {
		for ei < len(evs) && evs[ei].at <= n {
			e := evs[ei]
			if e.key >= 0 {
				m.InjectKey(byte(e.key))
			} else {
				m.SetMouseButtons(e.l, e.r, e.m)
				if e.dx != 0 || e.dy != 0 {
					m.InjectMouse(e.dx, e.dy)
				}
			}
			ei++
		}
		if len(feed) > 0 && n%1024 == 0 && (!feed[0].held || markSeen) && m.ConsoleRxIdle() {
			m.SendConsoleInput([]byte{feed[0].b})
			feed = feed[1:]
		}
		m.Tick()
		o := m.Sys.LastCPUOut
		if prevAS && !o.AS_n && o.ADDrive && !o.R_W &&
			(o.ST == zbus.StatusIO || o.ST == zbus.StatusSpecialIO) && o.AD == port {
			stopped = true
			n++
			break
		}
		if *marks && prevAS && !o.AS_n && o.ADDrive && !o.R_W &&
			(o.ST == zbus.StatusIO || o.ST == zbus.StatusSpecialIO) &&
			o.AD >= 0x0F00 && o.AD < 0x0FFC {
			id := int(o.AD-0x0F00) >> 1
			fmt.Fprintf(os.Stderr, "mark %d %d\n", id, n)
			if id == profFrom {
				profOn = true
			} else if id == profTo {
				profOn = false
			}
		}
		if profOn && n%16 == 0 {
			hist[m.Sys.CPU.DebugPC()]++
		}
		prevAS = o.AS_n
		if *idle != 0 && n-lastByte >= *idle {
			quiet = true
			n++
			break
		}
	}
	console.Flush()
	m.CloseHostSinks()
	el := time.Since(start).Seconds()
	fmt.Fprintf(os.Stderr, "simrun: %d ticks in %.1f s (%.2f Mticks/s), %s, PC %06X%s\n",
		n, el, float64(n)/el/1e6,
		why(stopped, quiet, *idle),
		m.Sys.CPU.DebugPC(), map[bool]string{true: " (halted)"}[m.Sys.CPU.DebugHalted()])
	if *profile != "" {
		if err := writeProfile(*profile, hist); err != nil {
			fmt.Fprintln(os.Stderr, "simrun: profile:", err)
		}
	}
	if *screen != "" {
		if err := writeScreen(m, *screen); err != nil {
			fmt.Fprintln(os.Stderr, "simrun: screen:", err)
		}
	}
	switch {
	case quiet:
		os.Exit(4)
	case !stopped:
		os.Exit(3)
	}
}
