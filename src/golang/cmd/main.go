package main

import (
	_ "embed"
	"io/ioutil"
	"os"

	winpmem "github.com/Velocidex/WinPmem"
	"github.com/alecthomas/kingpin"
)

var (
	app = kingpin.New("winpmem", "A Windows Memory Acquisition Tool.")

	service_name = app.Flag("service_name", "Name of the service to create").
			Default("winpmem").String()

	driver_path = app.Flag("driver_path", "Where to store the driver").
			String()

	filename = app.Arg("filename",
		"Output path to write image to").String()

	nosparse = app.Flag("nosparse", "Disable sparse output file").Bool()
	progress = app.Flag("progress", "Show progress").Bool()

	verbose = app.Flag("verbose", "More vebose information").Short('v').Bool()
)

func main() {
	app.HelpFlag.Short('h')
	app.UsageTemplate(kingpin.CompactUsageTemplate)

	args := os.Args[1:]
	kingpin.MustParse(app.Parse(args))

	err := doRun()
	kingpin.FatalIfError(err, "")
}

func doRun() error {
	logger := winpmem.NewLogger(*verbose)

	if *progress {
		logger.SetProgress(1024)
	}

	var err error
	var fd *os.File

	if *driver_path == "" {
		fd, err = ioutil.TempFile("", "*.sys")
		if err != nil {
			return err
		}

		defer func() {
			logger.Info("Removing driver from %v", fd.Name())
			err := os.Remove(fd.Name())
			if err != nil {
				logger.Info("Error removing %v: %v",
					fd.Name(), err)
			}
		}()

		*driver_path = fd.Name()

	} else {
		fd, err = os.OpenFile(*driver_path,
			os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0600)
		if err != nil {
			return err
		}
	}

	fd.Write([]byte(winpmem.Winpmem_x64))
	fd.Close()

	logger.Info("Writing driver to %v", *driver_path)

	err = winpmem.InstallDriver(*driver_path, *service_name, logger)

	defer winpmem.UninstallDriver(
		*driver_path, *service_name, logger)

	// We do not need to take the image - we are done.
	if *filename == "" {
		return nil
	}

	if !*nosparse {
		logger.Info("Setting sparse output file %v", *filename)
	}

	out_fd, err := winpmem.CreateFileForWriting(!*nosparse, *filename)
	if err != nil {
		return err
	}
	defer out_fd.Close()

	imager, err := winpmem.NewImager(`\\.\pmem`, logger)
	if err != nil {
		return err
	}
	defer imager.Close()

	// We only support this mode now - it is the most reliable.
	imager.SetMode(winpmem.PMEM_MODE_PTE)

	logger.Info("Memory Info:\n")
	logger.Info(imager.Stats().ToYaml())

	return imager.WriteTo(out_fd)
}
