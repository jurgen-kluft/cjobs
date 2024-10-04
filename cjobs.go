package main

import (
	"github.com/jurgen-kluft/ccode"
	cpkg "github.com/jurgen-kluft/cjobs/package"
)

func main() {
	if ccode.Init() {
		pkg := cpkg.GetPackage()
		ccode.GenerateFiles(pkg)
		ccode.Generate(pkg)
	}
}
