package winpmem

import (
	"fmt"
	"regexp"

	"gopkg.in/yaml.v2"
)

func CTL_CODE(device_type, function, method, access uint32) uint32 {
	return (device_type << 16) | (access << 14) | (function << 2) | method
}

const (
	NUMBER_OF_RUNS = 20
	PAGE_SIZE      = 0x1000

	BUFSIZE = PAGE_SIZE * 1024 // 4Mb
)

var (
	IOCTL_SET_MODE = CTL_CODE(0x22, 0x101, 3, 3)

	IOCTL_WRITE_ENABLE = CTL_CODE(0x22, 0x102, 3, 3)

	IOCTL_GET_INFO = CTL_CODE(0x22, 0x103, 3, 3)

	IOCTL_REVERSE_SEARCH_QUERY = CTL_CODE(0x22, 0x104, 3, 3)

	YamlFixup = regexp.MustCompile(`"(0x[a-f0-9]+)"`)
)

type PmemMode uint32

const (
	PMEM_MODE_IOSPACE  = PmemMode(0)
	PMEM_MODE_PHYSICAL = PmemMode(1)
	PMEM_MODE_PTE      = PmemMode(2)
)

type Run struct {
	Address int64
	Size    int64
	Sparse  bool
}

type PHYSICAL_MEMORY_RANGE struct {
	BaseAddress   Uint64Hex `yaml:"BaseAddress"`
	NumberOfBytes Uint64Hex `yaml:"NumberOfBytes"`
}

type WINPMEM_MEMORY_INFO_64 struct {
	CR3                 uint64
	NtBuildNumber       uint64
	KernelBase          uint64
	KDBG                uint64
	KPCR                [64]uint64
	PfnDataBase         uint64
	PsLoadedModuleList  uint64
	PsActiveProcessHead uint64
	NtBuildNumberAddr   uint64
	Padding             [0xfe]uint64
	NumberOfRuns        uint64
	Run                 [NUMBER_OF_RUNS]PHYSICAL_MEMORY_RANGE
}

func (self *WINPMEM_MEMORY_INFO_64) Info() *WinpmemInfo {
	res := &WinpmemInfo{
		CR3:               Uint64Hex(self.CR3),
		NtBuildNumber:     Uint64Hex(self.NtBuildNumber),
		KernelBase:        Uint64Hex(self.KernelBase),
		NtBuildNumberAddr: Uint64Hex(self.NtBuildNumberAddr),
	}

	for _, kpcr := range self.KPCR {
		if kpcr != 0 {
			res.KPCR = append(res.KPCR, Uint64Hex(kpcr))
		}
	}

	for i := 0; i < int(self.NumberOfRuns); i++ {
		if i > len(self.Run) {
			break
		}

		res.Run = append(res.Run, self.Run[i])
	}

	return res
}

type Uint64Hex uint64

func (self Uint64Hex) MarshalYAML() (interface{}, error) {
	return fmt.Sprintf("%#x", self), nil
}

type WinpmemInfo struct {
	CR3               Uint64Hex               `yaml:"CR3"`
	NtBuildNumber     Uint64Hex               `yaml:"NtBuildNumber"`
	KernelBase        Uint64Hex               `yaml:"KernelBase"`
	KPCR              []Uint64Hex             `yaml:"KPCR"`
	NtBuildNumberAddr Uint64Hex               `yaml:"NtBuildNumberAddr"`
	Run               []PHYSICAL_MEMORY_RANGE `yaml:"Run"`
}

func (self *WinpmemInfo) ToYaml() string {
	serialized, err := yaml.Marshal(self)
	if err != nil {
		return ""
	}

	return YamlFixup.ReplaceAllString(string(serialized), "$1")
}
