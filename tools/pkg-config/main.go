// Fake pkg-config for cgo builds targeting the Nintendo Switch. Only
// compiler flags matter: the c-archive is linked later by build.sh.
// The portlibs include path comes from NX_PORTLIBS_INCLUDE (env.sh).
package main

import (
	"fmt"
	"os"
)

func main() {
	portlibs := os.Getenv("NX_PORTLIBS_INCLUDE")
	if portlibs == "" {
		portlibs = "/opt/devkitpro/portlibs/switch/include"
	}
	cflags := false
	for _, a := range os.Args[1:] {
		if a == "--cflags" {
			cflags = true
		}
	}
	if cflags {
		fmt.Printf("-I%s -I%s/SDL2 -D_REENTRANT\n", portlibs, portlibs)
	} else {
		fmt.Println()
	}
}
