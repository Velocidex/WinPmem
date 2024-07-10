package main

import (
	"os"
	"time"

	"github.com/Velocidex/WinPmem/go-winpmem"
	"github.com/alecthomas/kingpin"
)

var (
	decompress = app.Command("extract", "Decompress an image.")
	image      = decompress.Arg("image", "Path to the image to decompress").
			Required().String()

	decompress_output_filename = decompress.Arg("filename",
		"Output path to write image to").Required().String()
)

func doDecompress() error {
	fd, err := os.Open(*image)
	if err != nil {
		return err
	}
	defer fd.Close()

	header := make([]byte, 10)
	n, err := fd.Read(header)
	if err != nil {
		return err
	}

	fd.Seek(0, os.SEEK_SET)

	decompressed_fd, err := winpmem.GetDecompressor(header[:n], fd)
	if err != nil {
		return err
	}

	out_fd, err := winpmem.CreateFileForWriting(true, *decompress_output_filename)
	if err != nil {
		return err
	}
	defer out_fd.Close()

	ctx, cancel := install_sig_handler()
	defer cancel()

	logger := &DecompressionLogger{Logger: winpmem.NewLogger(*verbose)}
	return winpmem.CopyAndLog(ctx, decompressed_fd, out_fd, logger)
}

func init() {
	command_handlers = append(command_handlers, func(command string) bool {
		switch command {
		case decompress.FullCommand():
			kingpin.FatalIfError(doDecompress(), "decompress")
		default:
			return false
		}
		return true
	})
}

type DecompressionLogger struct {
	winpmem.Logger
	count int
}

func (self *DecompressionLogger) Progress(pages int) {
	self.count += pages
	if self.count%20000 == 0 {
		self.Info("%v: Decompressed %v Mb", time.Now().Format(time.RFC3339),
			self.count*winpmem.PAGE_SIZE/1024/1024)
	}
}
