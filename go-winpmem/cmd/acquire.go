package main

import (
	"io/ioutil"
	"os"
	"time"

	"github.com/Velocidex/WinPmem/go-winpmem"
	"github.com/alecthomas/kingpin"
)

var (
	acquire = app.Command("acquire", "Acquire a memory image")

	service_name = acquire.Flag("service_name", "Name of the service to create").
			Default("winpmem").String()

	driver_path = acquire.Flag("driver_path", "Where to store the driver").
			String()

	filename = acquire.Arg("filename",
		"Output path to write image to").String()

	nosparse = acquire.Flag("nosparse", "Disable sparse output file").Bool()
	progress = acquire.Flag("progress", "Show progress").Bool()

	compression = acquire.Flag("compression", "Type of compression to apply").
			Default("none").PlaceHolder("snappy|gzip").String()
)

func doAcquire() error {
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

	driver_code, err := winpmem.Winpmem_x64()
	if err != nil {
		return err
	}

	fd.Write([]byte(driver_code))
	fd.Close()

	logger.Info("Writing driver to %v", *driver_path)

	err = winpmem.InstallDriver(*driver_path, *service_name, logger)

	defer winpmem.UninstallDriver(
		*driver_path, *service_name, logger)

	imager, err := winpmem.NewImager(`\\.\pmem`, logger)
	if err != nil {
		return err
	}
	defer imager.Close()

	// We only support this mode now - it is the most reliable.
	imager.SetMode(winpmem.PMEM_MODE_PTE)

	logger.Info("Memory Info:\n")
	logger.Info(imager.Stats().ToYaml())

	// We do not need to take the image - we are done.
	if *filename == "" {
		return nil
	}

	// Sparse writing is only possible with no compression.
	if *compression != "" && *compression != "none" {
		*nosparse = true
	}

	if !*nosparse {
		logger.Info("Setting sparse output file %v", *filename)
		imager.SetSparse()
	}

	out_fd, err := winpmem.CreateFileForWriting(!*nosparse, *filename)
	if err != nil {
		return err
	}
	defer out_fd.Close()

	start := time.Now()
	defer func() {
		logger.Info("Completed imaging in %v", time.Now().Sub(start))
	}()

	compressed_writer, closer, err := winpmem.GetCompressor(*compression, out_fd)
	if err != nil {
		return err
	}
	defer closer()

	ctx, cancel := install_sig_handler()
	defer cancel()

	return imager.WriteTo(ctx, compressed_writer)
}

func init() {
	command_handlers = append(command_handlers, func(command string) bool {
		switch command {
		case acquire.FullCommand():
			kingpin.FatalIfError(doAcquire(), "acquire")
		default:
			return false
		}
		return true
	})
}
