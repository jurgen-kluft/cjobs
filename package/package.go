package cjobs

import (
	callocator "github.com/jurgen-kluft/callocator/package"
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
	allocatorpkg := callocator.GetPackage()

	// The main (cjobs) package
	mainpkg := denv.NewPackage("cjobs")
	mainpkg.AddPackage(unittestpkg)
	mainpkg.AddPackage(entrypkg)
	mainpkg.AddPackage(basepkg)
	mainpkg.AddPackage(threadpkg)
	mainpkg.AddPackage(allocatorpkg)

	// 'cjobs' library
	mainlib := denv.SetupCppLibProject("cjobs", "github.com\\jurgen-kluft\\cjobs")
	mainlib.AddDependencies(basepkg.GetMainLib()...)
	mainlib.AddDependencies(threadpkg.GetMainLib()...)
	mainlib.AddDependencies(allocatorpkg.GetMainLib()...)

	// 'cjobs' unittest project
	maintest := denv.SetupDefaultCppTestProject("cjobs"+"_test", "github.com\\jurgen-kluft\\cjobs")
	maintest.AddDependencies(unittestpkg.GetMainLib()...)
	maintest.Dependencies = append(maintest.Dependencies, mainlib)

	mainpkg.AddMainLib(mainlib)
	mainpkg.AddUnittest(maintest)

	return mainpkg
}
