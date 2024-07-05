package winpmem

import (
	"fmt"
	"os"
)

type Logger interface {
	Info(format string, args ...interface{})
	Debug(format string, args ...interface{})

	Progress(pages int)
	SetProgress(pages_per_dot int)
}

type LogContext struct {
	debug bool

	pages_per_dot int
	subcount      int
	col           int
}

func (self *LogContext) Info(format string, args ...interface{}) {
	if self.col > 0 {
		os.Stdout.Write([]byte("\n"))
		self.col = 0
	}

	os.Stdout.Write([]byte(fmt.Sprintf(format, args...)))
	os.Stdout.Write([]byte("\n"))
}

func (self *LogContext) Debug(format string, args ...interface{}) {
	if self.debug {
		if self.col > 0 {
			os.Stdout.Write([]byte("\n"))
			self.col = 0
		}

		os.Stdout.Write([]byte(fmt.Sprintf(format, args...)))
		os.Stdout.Write([]byte("\n"))
	}
}

func (self *LogContext) Progress(pages int) {
	if self.pages_per_dot > 0 {
		for i := 0; i < pages; i++ {
			self._progress()
		}
	}
}

func (self *LogContext) _progress() {
	self.subcount++
	if self.subcount < self.pages_per_dot {
		return
	}

	os.Stdout.Write([]byte("."))
	self.subcount = 0

	self.col++
	if self.col > 80 {
		self.col = 0
		os.Stdout.Write([]byte("\n"))
	}
}

func (self *LogContext) SetProgress(pages_per_dot int) {
	self.pages_per_dot = pages_per_dot
}

func NewLogger(debug bool) Logger {
	return &LogContext{debug: debug}
}
