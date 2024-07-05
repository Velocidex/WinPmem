package winpmem

import (
	_ "embed"
)

//go:embed embed/winpmem_x64.sys
var Winpmem_x64 string

//go:embed embed/winpmem_x86.sys
var Winpmem_x86 string
