package winpmem

import (
	"bytes"
	"compress/gzip"
	_ "embed"
	"fmt"
	"io/ioutil"
)

//go:embed embed/winpmem_x64.sys.gz
var Winpmem_x64_gz string

/*
// go:embed embed/winpmem_x86.sys
// var Winpmem_x86 string
*/

func Winpmem_x64() (string, error) {
	reader := bytes.NewReader([]byte(Winpmem_x64_gz))
	gz_reader, err := gzip.NewReader(reader)
	if err != nil {
		return "", fmt.Errorf("Embedded driver corrupted: %w", err)
	}
	out, err := ioutil.ReadAll(gz_reader)
	if err != nil {
		return "", fmt.Errorf("Embedded driver corrupted: %w", err)
	}

	return string(out), nil
}
