package cjobs

import (
	cbase "github.com/jurgen-kluft/cbase/package"
	"github.com/jurgen-kluft/ccode/denv"
	centry "github.com/jurgen-kluft/centry/package"
	cthread "github.com/jurgen-kluft/cthread/package"
	cunittest "github.com/jurgen-kluft/cunittest/package"
)

// GetPackage returns the package object of 'cjobs'
func GetPackage() *denv.Package {
	// Dependencies
	unittestpkg := cunittest.GetPackage()
	entrypkg := centry.GetPackage()
	basepkg := cbase.GetPackage()
	threadpkg := cthread.GetPackage()

	// The main (cjobs) package
	mainpkg := denv.NewPackage("cjobs")
	mainpkg.AddPackage(unittestpkg)
	mainpkg.AddPackage(entrypkg)
	mainpkg.AddPackage(basepkg)
	mainpkg.AddPackage(threadpkg)

	// 'cjobs' library
	mainlib := denv.SetupDefaultCppLibProject("cjobs", "github.com\\jurgen-kluft\\cjobs")
	mainlib.Dependencies = append(mainlib.Dependencies, basepkg.GetMainLib())
	mainlib.Dependencies = append(mainlib.Dependencies, threadpkg.GetMainLib())

	// 'cjobs' unittest project
	maintest := denv.SetupDefaultCppTestProject("cjobs_test", "github.com\\jurgen-kluft\\cjobs")
	maintest.Dependencies = append(maintest.Dependencies, unittestpkg.GetMainLib())
	maintest.Dependencies = append(maintest.Dependencies, entrypkg.GetMainLib())
	maintest.Dependencies = append(maintest.Dependencies, basepkg.GetMainLib())
	maintest.Dependencies = append(maintest.Dependencies, threadpkg.GetMainLib())
	maintest.Dependencies = append(maintest.Dependencies, mainlib)

	mainpkg.AddMainLib(mainlib)
	mainpkg.AddUnittest(maintest)

	return mainpkg
}
