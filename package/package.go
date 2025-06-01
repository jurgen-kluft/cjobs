package cjobs

import (
	callocator "github.com/jurgen-kluft/callocator/package"
	"github.com/jurgen-kluft/ccode/denv"
	cthread "github.com/jurgen-kluft/cthread/package"
	cunittest "github.com/jurgen-kluft/cunittest/package"
)

const (
	repo_path = "github.com\\jurgen-kluft"
	repo_name = "cjobs"
)

func GetPackage() *denv.Package {
	name := repo_name

	// dependencies
	cunittestpkg := cunittest.GetPackage()
	callocpkg := callocator.GetPackage()
	cthreadpkg := cthread.GetPackage()

	// main package
	mainpkg := denv.NewPackage(repo_path, repo_name)
	mainpkg.AddPackage(cunittestpkg)
	mainpkg.AddPackage(callocpkg)
	mainpkg.AddPackage(cthreadpkg)

	// main library
	mainlib := denv.SetupCppLibProject(mainpkg, name)
	mainlib.AddDependencies(callocpkg.GetMainLib()...)
	mainlib.AddDependencies(cthreadpkg.GetMainLib()...)

	// test library
	testlib := denv.SetupCppTestLibProject(mainpkg, name)
	mainlib.AddDependencies(callocpkg.GetTestLib()...)
	mainlib.AddDependencies(cthreadpkg.GetTestLib()...)
	testlib.AddDependencies(cunittestpkg.GetTestLib()...)

	// unittest project
	maintest := denv.SetupCppTestProject(mainpkg, name)
	maintest.AddDependencies(cunittestpkg.GetMainLib()...)
	maintest.AddDependency(testlib)

	mainpkg.AddMainLib(mainlib)
	mainpkg.AddTestLib(testlib)
	mainpkg.AddUnittest(maintest)
	return mainpkg
}
