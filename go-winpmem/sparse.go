package winpmem

import (
	"io"
	"syscall"

	"golang.org/x/sys/windows"
)

type WriteSeekCloser interface {
	io.Writer
	io.Seeker
	io.Closer
}

type WindowsFile struct {
	fd windows.Handle
}

func (self *WindowsFile) Seek(offset int64, whence int) (int64, error) {
	return windows.Seek(self.fd, offset, whence)
}

func (self *WindowsFile) Write(buf []byte) (int, error) {
	var actual_written uint32
	err := windows.WriteFile(self.fd, buf, &actual_written, nil)
	return int(actual_written), err
}

func (self *WindowsFile) Close() error {
	return windows.CloseHandle(self.fd)
}

func CreateFileForWriting(sparse bool, path string) (WriteSeekCloser, error) {
	var nullHandle windows.Handle
	path_utf16, err := syscall.UTF16FromString(path)
	if err != nil {
		return nil, err
	}

	fd, err := windows.CreateFile(
		&path_utf16[0], windows.GENERIC_WRITE|windows.GENERIC_WRITE,
		windows.FILE_SHARE_READ|windows.FILE_SHARE_WRITE,
		nil,
		windows.CREATE_ALWAYS,
		windows.FILE_ATTRIBUTE_NORMAL,
		nullHandle,
	)
	if err != nil {
		return nil, err
	}

	var bytesReturned uint32

	if sparse {
		err := syscall.DeviceIoControl(
			syscall.Handle(fd), windows.FSCTL_SET_SPARSE,
			nil, 0, nil, 0,
			&bytesReturned, nil)
		if err != nil {
			windows.CloseHandle(fd)
			return nil, err
		}
	}

	return &WindowsFile{fd: fd}, nil
}
