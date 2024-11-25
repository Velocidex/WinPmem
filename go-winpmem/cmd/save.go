package main

import (
	"os"

	"github.com/Velocidex/WinPmem/go-winpmem"
	"github.com/alecthomas/kingpin"
)

var (
	save = app.Command("save", "Save the driver and exit")

	save_driver_path = save.Arg("driver_path", "Where to store the driver").
				String()
)

func doSave() error {
	logger := winpmem.NewLogger(*verbose)
	logger.Info("Writing driver to %v", *save_driver_path)
	fd, err := os.OpenFile(*save_driver_path,
		os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0600)
	if err != nil {
		return err
	}
	driver_code, err := winpmem.Winpmem_x64()
	if err != nil {
		return err
	}

	fd.Write([]byte(driver_code))
	fd.Close()

	return nil
}

func init() {
	command_handlers = append(command_handlers, func(command string) bool {
		switch command {
		case save.FullCommand():
			kingpin.FatalIfError(doSave(), "save")

		default:
			return false
		}
		return true
	})
}
