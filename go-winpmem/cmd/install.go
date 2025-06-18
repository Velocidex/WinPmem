package main

import (
	"io/ioutil"
	"os"

	"github.com/Velocidex/WinPmem/go-winpmem"
	"github.com/alecthomas/kingpin"
)

var (
	install = app.Command("install", "Install the driver and exit")

	install_driver_path = install.Flag("driver_path", "Where to store the driver").
				String()

	install_service_name = install.Flag("service_name", "Name of the service to create").
				Default("winpmem").String()

	uninstall = app.Command("uninstall", "Uninstall the driver and exit")

	uninstall_driver_path = uninstall.Flag("driver_path", "Where to store the driver").
				String()

	uninstall_service_name = uninstall.Flag("service_name", "Name of the service to create").
				Default("winpmem").String()
)

func doInstall() error {
	logger := winpmem.NewLogger(*verbose)

	var err error
	var fd *os.File

	if *install_driver_path == "" {
		fd, err = ioutil.TempFile("", "*.sys")
		if err != nil {
			return err
		}

		*install_driver_path = fd.Name()

	} else {
		fd, err = os.OpenFile(*install_driver_path,
			os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0600)
		if err != nil {
			return err
		}
	}

	driver_code, err := winpmem.Winpmem_x64()
	if err != nil {
		return err
	}

	fd.Write([]byte(driver_code))
	fd.Close()

	logger.Info("Writing driver to %v", *install_driver_path)

	err = winpmem.InstallDriver(*install_driver_path, *install_service_name, logger)
	if err != nil {
		return err
	}

	return nil
}

func doUninstall() error {
	logger := winpmem.NewLogger(*verbose)

	winpmem.UninstallDriver(*uninstall_driver_path, *uninstall_service_name, logger)

	return nil
}

func init() {
	command_handlers = append(command_handlers, func(command string) bool {
		switch command {
		case install.FullCommand():
			kingpin.FatalIfError(doInstall(), "install")
		case uninstall.FullCommand():
			kingpin.FatalIfError(doUninstall(), "uninstall")

		default:
			return false
		}
		return true
	})
}
