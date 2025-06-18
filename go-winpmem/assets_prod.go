//go:build !testdriver

package winpmem

import (
	_ "embed"
)

//go:embed embed/winpmem_x64.sys.gz
var Winpmem_x64_gz string
