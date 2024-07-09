package main

import (
	_ "embed"
	"os"

	"github.com/alecthomas/kingpin"
)

var (
	app = kingpin.New("winpmem", "A Windows Memory Acquisition Tool.")

	verbose = app.Flag("verbose", "More vebose information").Short('v').Bool()

	command_handlers []CommandHandler
)

type CommandHandler func(command string) bool

func main() {
	app.HelpFlag.Short('h')
	app.UsageTemplate(kingpin.CompactUsageTemplate)

	command := kingpin.MustParse(app.Parse(os.Args[1:]))

	for _, command_handler := range command_handlers {
		if command_handler(command) {
			break
		}
	}
}
