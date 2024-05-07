package main

import (
	"github.com/jurgen-kluft/ccode"
	"github.com/jurgen-kluft/cjobs/package"
)

func main() {
	ccode.Init()
	ccode.GenerateFiles()
	ccode.Generate(cjobs.GetPackage())
}
