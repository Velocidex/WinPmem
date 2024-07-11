package winpmem

import (
	"bytes"
	"encoding/binary"
	"io"
	"os"
	"syscall"

	"golang.org/x/sys/windows"
)

type Imager struct {
	fd windows.Handle

	stats *WinpmemInfo

	logger Logger
}

func (self *Imager) SetMode(mode PmemMode) error {
	var length uint32
	var buff []byte

	buff = binary.LittleEndian.AppendUint32(buff, uint32(mode))

	return windows.DeviceIoControl(self.fd,
		IOCTL_SET_MODE, &buff[0], 4, nil, 0, &length, nil)
}

func (self *Imager) Stats() *WinpmemInfo {
	return self.stats
}

func (self *Imager) getStats() (*WinpmemInfo, error) {
	buff := make([]byte, 1024*64)
	var length uint32

	err := windows.DeviceIoControl(self.fd,
		IOCTL_GET_INFO,
		nil, 0, &buff[0], uint32(len(buff)),
		&length, nil)
	if err != nil {
		return nil, err
	}

	info := WINPMEM_MEMORY_INFO_64{}
	reader := bytes.NewReader(buff)
	err = binary.Read(reader, binary.LittleEndian, &info)
	if err != nil {
		return nil, err
	}

	return info.Info(), nil
}

func (self *Imager) pad(size uint64, w io.Writer) error {
	write_seeker, ok := w.(io.WriteSeeker)
	if ok {
		self.logger.Progress(int(size / PAGE_SIZE))

		// Support sparse files if the filesystem allows it
		_, err := write_seeker.Seek(int64(size), os.SEEK_CUR)
		return err
	}

	for offset := uint64(0); offset < size; {
		to_write := size - offset
		if to_write > BUFSIZE {
			to_write = BUFSIZE
		}

		buff := make([]byte, to_write)
		n, err := w.Write(buff)
		if err != nil {
			return err
		}

		self.logger.Progress(int(to_write / PAGE_SIZE))

		offset += uint64(n)
	}

	return nil
}

// copyRange copies a range from the base_addr to the writer. We
// assume size is a multiple of PAGE_SIZE
func (self *Imager) copyRange(base_addr, size uint64, w io.Writer) error {
	buff := make([]byte, BUFSIZE)
	pad := make([]byte, PAGE_SIZE)
	end := base_addr + size

	for offset := base_addr; offset < end; {
		to_read := end - offset
		if to_read > BUFSIZE {
			to_read = BUFSIZE
		}

		actual_read := uint32(0)

		self.logger.Debug("Reading %#x from %#x", to_read, offset)
		_, err := windows.Seek(self.fd, int64(offset), os.SEEK_SET)
		if err != nil {
			return err
		}

		err = windows.ReadFile(self.fd, buff[:to_read], &actual_read, nil)
		if err != nil {
			// Large Read failed, read in pages and pad any failed pages
			for i := offset; i < offset+to_read; i += PAGE_SIZE {

				_, err = windows.Seek(self.fd, int64(i), os.SEEK_SET)
				if err != nil {
					return err
				}
				err := windows.ReadFile(self.fd, buff[:PAGE_SIZE], &actual_read, nil)
				if err != nil {
					_, err := w.Write(pad)
					if err != nil {
						return err
					}
					continue
				}

				// Cant really happen but we can check anyway.
				if actual_read > uint32(len(buff)) {
					actual_read = uint32(len(buff))
				}

				self.logger.Progress(int(actual_read / PAGE_SIZE))

				_, err = w.Write(buff[:actual_read])
				if err != nil {
					return err
				}
			}
			offset += uint64(to_read)

		} else {
			// Large read succeeded - just copy the whole buffer to the writer.

			// Cant really happen but we can check anyway.
			if actual_read > uint32(len(buff)) {
				actual_read = uint32(len(buff))
			}

			self.logger.Progress(int(actual_read / PAGE_SIZE))

			_, err := w.Write(buff[:actual_read])
			if err != nil {
				return err
			}
			offset += uint64(actual_read)
		}

	}

	return nil
}

func (self *Imager) WriteTo(w io.Writer) error {
	var offset uint64
	for _, r := range self.stats.Run {
		base_addr := uint64(r.BaseAddress)
		number_of_bytes := uint64(r.NumberOfBytes)

		// Pad up to the next range
		if offset < base_addr {
			pad_size := base_addr - offset

			self.logger.Info(
				"Padding %v pages from %#x", pad_size/PAGE_SIZE, offset)

			err := self.pad(pad_size, w)
			if err != nil {
				return err
			}
			offset = base_addr
		}

		self.logger.Info(
			"Copying %v pages from %#x", number_of_bytes/PAGE_SIZE, offset)

		err := self.copyRange(base_addr, number_of_bytes, w)
		if err != nil {
			return err
		}
	}
	return nil
}

func (self *Imager) Close() {
	windows.CloseHandle(self.fd)
}

func NewImager(
	device_name string,
	logger Logger) (*Imager, error) {
	var nullHandle windows.Handle

	device_name_utf16, err := syscall.UTF16FromString(device_name)
	if err != nil {
		return nil, err
	}
	fd, err := windows.CreateFile(&device_name_utf16[0], windows.GENERIC_READ|windows.GENERIC_WRITE,
		windows.FILE_SHARE_READ|windows.FILE_SHARE_WRITE,
		nil,
		windows.OPEN_EXISTING,
		windows.FILE_ATTRIBUTE_NORMAL,
		nullHandle,
	)
	if err != nil {
		return nil, err
	}

	res := &Imager{
		fd:     fd,
		logger: logger,
	}

	res.stats, err = res.getStats()
	if err != nil {
		res.Close()
		return nil, err
	}

	return res, nil
}
