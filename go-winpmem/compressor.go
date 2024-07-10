package winpmem

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"strings"

	"github.com/klauspost/compress/gzip"
	"github.com/klauspost/compress/s2"
	"github.com/klauspost/compress/snappy"
)

func GetCompressor(name string, w io.Writer) (io.Writer, func(), error) {
	name = strings.ToLower(name)
	switch name {
	case "", "stored", "none":
		return w, func() {}, nil

	case "s2":
		res := s2.NewWriter(w)
		return res, func() {
			res.Close()
		}, nil

	case "snappy":
		res := snappy.NewWriter(w)
		return res, func() {
			res.Close()
		}, nil

	case "gzip", "gz":
		res, err := gzip.NewWriterLevel(w, gzip.BestSpeed)
		if err != nil {
			return nil, nil, err
		}
		return res, func() {
			res.Close()
		}, nil

	default:
		return nil, nil, fmt.Errorf("Compression method %v not supported. Valid methods are: none, snappy, s2, gzip, gz", name)
	}
}

var (
	SNAPPY = []byte{0xFF, 0x06, 0x00, 0x00, 0x73, 0x4E, 0x61, 0x50, 0x70, 0x59}
	S2     = []byte{0xFF, 0x06, 0x00, 0x00, 0x53, 0x32, 0x73, 0x54, 0x77, 0x4F}
	GZIP   = []byte{0x1F, 0x8B, 0x08}
)

func GetDecompressor(header []byte, r io.Reader) (io.Reader, error) {
	if bytes.HasPrefix(header, SNAPPY) {
		return snappy.NewReader(r), nil
	}

	if bytes.HasPrefix(header, S2) {
		return s2.NewReader(r), nil
	}

	if bytes.HasPrefix(header, GZIP) {
		return gzip.NewReader(r)
	}

	return nil, errors.New("Unknown compression scheme")
}

func CopyAndLog(
	ctx context.Context, in io.Reader, out io.Writer, logger Logger) error {
	buff := make([]byte, 1024*PAGE_SIZE)
	for {
		n, err := in.Read(buff)
		if err != nil && !errors.Is(err, io.EOF) {
			return err
		}

		if n == 0 {
			return nil
		}

		logger.Progress(n / PAGE_SIZE)
		_, err = out.Write(buff[:n])
		if err != nil {
			return err
		}

		select {
		case <-ctx.Done():
			return errors.New("Cancelled!")
		default:
		}
	}
}
