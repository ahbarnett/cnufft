# Makefile for FINUFFT

# For simplicity, this is the only makefile; there are no makefiles in
# subdirectories. This makefile is useful to show humans how to compile
# FINUFFT and its various language interfaces and examples.
# Users should not need to edit this makefile (doing so would make it hard to
# stay up to date with the repo version). Rather, in order to change
# OS/environment-specific compilers and flags, create the file make.inc, which
# overrides the defaults below (which are for an ubuntu linux/GCC system).
# See docs/install.rst, and make.inc.* for examples.

# Barnett 2017-2020. Malleo's expansion for guru interface, summer 2019.
# Barnett tidying Feb, May 2020. Libin Lu edits, 2020.
# Garrett Wright, Joakim Anden, Barnett: dual-prec lib build, Jun-Jul'20.
# Windows compatibility, jonas-kr, Sep '20.

# Compiler (CXX), and linking from C, fortran. We use GCC by default...
CXX = g++
CC = gcc
FC = gfortran
CLINK = -lstdc++
FLINK = $(CLINK)
# Python version: we use python3 by default, but you may need to change...
PYTHON = python3
# baseline compile flags for GCC (no multithreading):
# Notes: 1) -Ofast breaks isfinite() & isnan(), so use -O3 which now is as fast
#        2) -fcx-limited-range for fortran-speed complex arith in C++
#        3) we use simply-expanded (:=) makefile variables, otherwise confusing
CFLAGS := -O3 -funroll-loops -march=native -fcx-limited-range
FFLAGS := $(CFLAGS)
CXXFLAGS := $(CFLAGS)
# put this in your make.inc if you have FFTW>=3.3.5 and want thread-safe use...
#CXXFLAGS += -DFFTW_PLAN_SAFE
# FFTW base name, and math linking...
FFTWNAME = fftw3
# linux default is fftw3_omp, since 10% faster than fftw3_threads...
FFTWOMPSUFFIX = omp
LIBS := -lm
# multithreading for GCC: C++/C/Fortran, MATLAB, and octave (ICC differs)...
OMPFLAGS = -fopenmp
OMPLIBS = -lgomp
MOMPFLAGS = -D_OPENMP
OOMPFLAGS =
# MATLAB MEX compilation (also see below +=)...
MFLAGS := -largeArrayDims
# location of MATLAB's mex compiler (could add flags to switch GCC, etc)...
MEX = mex
# octave, and its mkoctfile and flags (also see below +=)...
OCTAVE = octave
MKOCTFILE = mkoctfile
OFLAGS =
# For experts only, location of MWrap executable (see docs/install.rst):
MWRAP = mwrap
# absolute path of this makefile, ie FINUFFT's top-level directory...
FINUFFT = $(dir $(realpath $(firstword $(MAKEFILE_LIST))))

# For your OS, override the above by setting make variables in make.inc ...
# (Please look in make.inc.* for ideas)
-include make.inc

# Now come flags that should be added, whatever user overrode in make.inc.
# -fPIC (position-indep code) needed to build dyn lib (.so)
# Also, we force return (via :=) to the land of simply-expanded variables...
INCL = -Iinclude
CXXFLAGS := $(CXXFLAGS) $(INCL) -fPIC -std=c++14
CFLAGS := $(CFLAGS) $(INCL) -fPIC
# here /usr/include needed for fftw3.f "fortran header"...
FFLAGS := $(FFLAGS) $(INCL) -I/usr/include -fPIC

# single-thread total list of math and FFTW libs (now both precisions)...
# (Note: finufft tests use LIBSFFT; spread & util tests only need LIBS)
LIBSFFT := -l$(FFTWNAME) $(LIBS)
# Detect single-precision library (not available on all systems)
ALLOW_SINGLE := 0
ifneq ("$(wildcard $(FFTW_DIR)/*$(FFTWNAME)f*)","")
    $(info detected $(FFTWNAME)f library -- building with lib$(FFTWNAME)f support)
    LIBSFFT += -l$(FFTWNAME)f
    ALLOW_SINGLE := 1
endif



# multi-threaded libs & flags, and req'd flags (OO for new interface)...
ifneq ($(OMP),OFF)
CXXFLAGS += $(OMPFLAGS)
CFLAGS += $(OMPFLAGS)
FFLAGS += $(OMPFLAGS)
MFLAGS += $(MOMPFLAGS) -DR2008OO
OFLAGS += $(OOMPFLAGS) -DR2008OO
LIBS += $(OMPLIBS)
ifneq ($(MINGW),ON)
ifneq ($(MSYS),ON)
# omp override for total list of math and FFTW libs (possibly both precisions)...
LIBSFFT := -l$(FFTWNAME) -l$(FFTWNAME)_$(FFTWOMPSUFFIX)  $(LIBS)
# Detect single-precision library (not available on all systems)
ALLOW_SINGLE := 0
ifneq ("$(wildcard $(FFTW_DIR)/*$(FFTWNAME)f*)","")
    $(info detected $(FFTWNAME)f library -- building with lib$(FFTWNAME)f support)
    LIBSFFT += -l$(FFTWNAME)f -l$(FFTWNAME)f_$(FFTWOMPSUFFIX)
    ALLOW_SINGLE := 1
endif
endif
endif
endif

# name & location of library we're building...
LIBNAME = libfinufft
DYNLIB = lib/$(LIBNAME).so
STATICLIB = lib-static/$(LIBNAME).a
# absolute path to the .so, useful for linking so executables portable...
ABSDYNLIB = $(FINUFFT)$(DYNLIB)

# spreader is subset of the library with self-contained testing, hence own objs:
# double-prec spreader object files that also need single precision...
SOBJS = src/spreadinterp.o src/utils.o
# their single-prec versions
ifeq ($(ALLOW_SINGLE),1)
    SOBJSF = $(SOBJS:%.o=%_32.o)
endif
# precision-dependent spreader object files (compiled & linked only once)...
SOBJS_PI = src/utils_precindep.o
# spreader dual-precision objs
SOBJSD = $(SOBJS) $(SOBJSF) $(SOBJS_PI)

# double-prec library object files that also need single precision...
OBJS = $(SOBJS) src/finufft.o src/simpleinterfaces.o fortran/finufftfort.o
# their single-prec versions
ifeq ($(ALLOW_SINGLE),1)
    OBJSF = $(OBJS:%.o=%_32.o)
endif
# precision-dependent library object files (compiled & linked only once)...
OBJS_PI = $(SOBJS_PI) contrib/legendre_rule_fast.o julia/finufftjulia.o
# all lib dual-precision objs
OBJSD = $(OBJS) $(OBJSF) $(OBJS_PI)

.PHONY: usage lib examples test perftest spreadtest spreadtestall fortran matlab octave all mex python clean objclean pyclean mexclean wheel docker-wheel gurutime docs

default: usage

all: test perftest lib examples fortran matlab octave python

usage:
	@echo "Makefile for FINUFFT library. Please specify your task:"
	@echo " make lib - build the main library (in lib/ and lib-static/)"
	@echo " make examples - compile and run codes in examples/"
	@echo " make test - compile and run quick math validation tests"
	@echo " make perftest - compile and run (slower) performance tests"
	@echo " make fortran - compile and run Fortran tests and examples"
	@echo " make matlab - compile MATLAB interfaces (no test)"
	@echo " make octave - compile and test octave interfaces"
	@echo " make python - compile and test python interfaces"
	@echo " make all - do all the above (around 1 minute; assumes you have MATLAB, etc)"
	@echo " make spreadtest - compile & run spreader-only tests (no FFTW)"
	@echo " make spreadtestall - set of spreader-only tests for CI use"
	@echo " make objclean - remove all object files, preserving libs & MEX"
	@echo " make clean - also remove all lib, MEX, py, and demo executables"
	@echo "For faster (multicore) making, append, for example, -j8"
	@echo ""
	@echo "Make options:"
	@echo " 'make [task] OMP=OFF' for single-threaded (otherwise OpenMP)"
	@echo " You must 'make objclean' before changing such options!"
	@echo ""
	@echo "Also see docs/install.rst"

# collect headers for implicit depends
HEADERS = $(wildcard include/*.h)

# implicit rules for objects (note -o ensures writes to correct dir)
%.o: %.cpp $(HEADERS)
	$(CXX) -c $(CXXFLAGS) $< -o $@
ifeq ($(ALLOW_SINGLE),1)
%_32.o: %.cpp $(HEADERS)
	$(CXX) -DSINGLE -c $(CXXFLAGS) $< -o $@
endif
%.o: %.c $(HEADERS)
	$(CC) -c $(CFLAGS) $< -o $@
ifeq ($(ALLOW_SINGLE),1)
%_32.o: %.c $(HEADERS)
	$(CC) -DSINGLE -c $(CFLAGS) $< -o $@
endif
%.o: %.f
	$(FC) -c $(FFLAGS) $< -o $@
ifeq ($(ALLOW_SINGLE),1)
%_32.o: %.f
	$(FC) -DSINGLE -c $(FFLAGS) $< -o $@
endif

# included auto-generated code dependency...
src/spreadinterp.o: src/ker_horner_allw_loop.c src/ker_lowupsampfac_horner_allw_loop.c


# lib -----------------------------------------------------------------------
# build library with double/single prec both bundled in...
lib: $(STATICLIB) $(DYNLIB)
$(STATICLIB): $(OBJSD)
	ar rcs $(STATICLIB) $(OBJSD)
ifeq ($(OMP),OFF)
	@echo "$(STATICLIB) built, single-thread version"
else
	@echo "$(STATICLIB) built, multithreaded version"
endif
$(DYNLIB): $(OBJSD)
# using *absolute* path in the -o here is needed to make portable executables
# when compiled against it, in mac OSX, strangely...
	$(CXX) -shared $(OMPFLAGS) $(OBJSD) -o $(ABSDYNLIB) $(LIBSFFT)
ifeq ($(OMP),OFF)
	@echo "$(DYNLIB) built, single-thread version"
else
	@echo "$(DYNLIB) built, multithreaded version"
endif

# here $(OMPFLAGS) and $(LIBSFFT) is even needed for linking under mac osx.
# see: http://www.cprogramming.com/tutorial/shared-libraries-linux-gcc.html
# Also note -l libs come after objects, as per modern GCC requirement.


# examples (C++/C) -----------------------------------------------------------
# single-prec codes separate, and not all have one
EXAMPLES = $(basename $(wildcard examples/*.*))
examples: $(EXAMPLES)
ifneq ($(MINGW),ON)
    # Windows-MSYS does not find the dynamic libraries, so we make a temporary copy
	# Windows-MSYS has same commands as Linux/OSX
    ifeq ($(MSYS),ON)
	    cp $(DYNLIB) test
    endif
# non-Windows-WSL: this task always runs them (note escaped $ to pass to bash)...
	for i in $(EXAMPLES); do echo $$i...; ./$$i; done
else
# Windows-WSL does not find the dynamic libraries, so we make a temporary copy
	copy $(DYNLIB) examples
	for /f "delims= " %%i in ("$(subst /,\,$(EXAMPLES))") do (echo %%i & %%i.exe)
	del examples\$(LIBNAME).so
endif
	@echo "Done running: $(EXAMPLES)"
# fun fact: gnu make patterns match those with shortest "stem", so this works:
examples/%: examples/%.o $(DYNLIB)
	$(CXX) $(CXXFLAGS) $< $(ABSDYNLIB) -o $@
examples/%c: examples/%c.o $(DYNLIB)
	$(CC) $(CFLAGS) $< $(ABSDYNLIB) $(LIBSFFT) $(CLINK) -o $@
examples/%cf: examples/%cf.o $(DYNLIB)
	$(CC) $(CFLAGS) $< $(ABSDYNLIB) $(LIBSFFT) $(CLINK) -o $@


# test (library validation) --------------------------------------------------
# build (skipping .o) but don't run. Run with 'test' target
# Note: both precisions use same sources; single-prec executables get f suffix.
# generic tests link against our .so... (other libs needed for fftw_forget...)
test/%: test/%.cpp $(DYNLIB)
	$(CXX) $(CXXFLAGS) $< $(ABSDYNLIB) $(LIBSFFT) -o $@
test/%f: test/%.cpp $(DYNLIB)
	$(CXX) $(CXXFLAGS) -DSINGLE $< $(ABSDYNLIB) $(LIBSFFT) -o $@
# low-level tests that are cleaner if depend on only specific objects...
test/testutils: test/testutils.cpp src/utils.o src/utils_precindep.o
	$(CXX) $(CXXFLAGS) test/testutils.cpp src/utils.o src/utils_precindep.o $(LIBS) -o test/testutils
ifeq ($(ALLOW_SINGLE),1)
test/testutilsf: test/testutils.cpp src/utils_32.o src/utils_precindep.o
	$(CXX) $(CXXFLAGS) -DSINGLE test/testutils.cpp src/utils_32.o src/utils_precindep.o $(LIBS) -o test/testutilsf
endif

# make sure all double-prec test executables ready for testing
TESTS := $(basename $(wildcard test/*.cpp))
# also need single-prec
TESTS += $(TESTS:%=%f)
test: $(TESTS)
ifneq ($(MINGW),ON)
# non-Windows-WSL: it will fail if either of these return nonzero exit code...
    # Windows-MSYS does not find the dynamic libraries, so we make a temporary copy
	# Windows-MSYS has same commands as Linux/OSX
    ifeq ($(MSYS),ON)
	    cp $(DYNLIB) test
    endif
	test/basicpassfail
	test/basicpassfailf
# accuracy tests done in prec-switchable bash script... (small prob -> few thr)
	(cd test; export OMP_NUM_THREADS=4; ./check_finufft.sh; ./check_finufft.sh SINGLE)
else
# Windows-WSL does not find the dynamic libraries, so we make a temporary copy...
	copy $(DYNLIB) test
	test/basicpassfail
	test/basicpassfailf
# Windows does not feature a bash shell so we use WSL. Since most supplied gnu-make variants are 32bit executables and WSL runs only in 64bit environments, we have to refer to 64bit powershell explicitly on 32bit make...
#	$(windir)\Sysnative\WindowsPowerShell\v1.0\powershell.exe "cd ./test; bash check_finufft.sh DOUBLE $(MINGW); bash check_finufft.sh SINGLE $(MINGW)"
# with a recent version of gnu-make for Windows built for 64bit as it is part of the WinLibs standalone build of GCC and MinGW-w64 we can avoid these circumstances
	cd test
	bash -c "cd test; ./check_finufft.sh DOUBLE $(MINGW)"
	bash -c "cd test; ./check_finufft.sh SINGLE $(MINGW)"
	del test\$(LIBNAME).so
endif


# perftest (performance/developer tests) -------------------------------------
# generic perf test rules...
perftest/%: perftest/%.cpp $(DYNLIB)
	$(CXX) $(CXXFLAGS) $< $(ABSDYNLIB) $(LIBSFFT) -o $@
ifeq ($(ALLOW_SINGLE),1)
perftest/%f: perftest/%.cpp $(DYNLIB)
	$(CXX) $(CXXFLAGS) -DSINGLE $< $(ABSDYNLIB) $(LIBSFFT) -o $@
endif

# spreader only test, double/single (good for self-contained work on spreader)
ST=perftest/spreadtestnd
STF=$(ST)f
$(ST): $(ST).cpp $(SOBJS) $(SOBJS_PI)
	$(CXX) $(CXXFLAGS) $< $(SOBJS) $(SOBJS_PI) $(LIBS) -o $@
ifeq ($(ALLOW_SINGLE),1)
$(STF): $(ST).cpp $(SOBJSF) $(SOBJS_PI)
	$(CXX) $(CXXFLAGS) -DSINGLE $< $(SOBJSF) $(SOBJS_PI) $(LIBS) -o $@
endif
spreadtest: $(ST) $(STF)
# run one thread per core... (escape the $ to get single $ in bash; one big cmd)
	(export OMP_NUM_THREADS=$$(perftest/mynumcores.sh) ;\
	echo "\nRunning makefile double-precision spreader tests, $$OMP_NUM_THREADS threads..." ;\
	$(ST) 1 8e6 8e6 1e-6 ;\
	$(ST) 2 8e6 8e6 1e-6 ;\
	$(ST) 3 8e6 8e6 1e-6 ;\
	echo "\nRunning makefile single-precision spreader tests, $$OMP_NUM_THREADS threads..." ;\
	$(STF) 1 8e6 8e6 1e-3 ;\
	$(STF) 2 8e6 8e6 1e-3 ;\
	$(STF) 3 8e6 8e6 1e-3 )
spreadtestall: $(ST) $(STF)
	(cd perftest; ./spreadtestall.sh)

PERFEXECS := $(basename $(wildcard test/finufft?d_test.cpp))
PERFEXECS += $(PERFEXECS:%=%f)
perftest: $(ST) $(STF) $(PERFEXECS)
# here the tee cmd copies output to screen. 2>&1 grabs both stdout and stderr...
	(cd perftest ;\
	./spreadtestnd.sh 2>&1 | tee results/spreadtestnd_results.txt ;\
	./spreadtestnd.sh SINGLE 2>&1 | tee results/spreadtestndf_results.txt ;\
	./nuffttestnd.sh 2>&1 | tee results/nuffttestnd_results.txt ;\
	./nuffttestnd.sh SINGLE 2>&1 | tee results/nuffttestndf_results.txt )

# speed ratio of many-vector guru vs repeated single calls... (Andrea)
GTT=perftest/guru_timing_test
GTTF=$(GTT)f
gurutime: $(GTT) $(GTTF)
	for i in $(GTT) $(GTTF); do $$i 100 1 2 1e2 1e2 0 1e6 1e-3 1 0 0 2; done

# This was for a CCQ application... (zgemm was 10x faster! double-prec only)
perftest/manysmallprobs: perftest/manysmallprobs.cpp $(STATICLIB)
	$(CXX) $(CXXFLAGS) $< $(STATICLIB) $(LIBSFFT) -o $@
	@echo "manysmallprobs: single-thread..."
	OMP_NUM_THREADS=1 $@




# ======================= LANGUAGE INTERFACES ==============================

# fortran --------------------------------------------------------------------
FD = fortran/directft
# CMCL NUFFT fortran test codes (only needed by the nufft*_demo* codes)
CMCLOBJS = $(FD)/dirft1d.o $(FD)/dirft2d.o $(FD)/dirft3d.o $(FD)/dirft1df.o $(FD)/dirft2df.o $(FD)/dirft3df.o $(FD)/prini.o
FE_DIR = fortran/examples
FE64 = $(FE_DIR)/simple1d1 $(FE_DIR)/guru1d1 $(FE_DIR)/nufft1d_demo $(FE_DIR)/nufft2d_demo $(FE_DIR)/nufft3d_demo $(FE_DIR)/nufft2dmany_demo
FE32 = $(FE64:%=%f)
# all the fortran examples...
FE = $(FE64) $(FE32)

# fortran target pattern match
$(FE_DIR)/%: $(FE_DIR)/%.f $(CMCLOBJS) $(DYNLIB)
	$(FC) $(FFLAGS) $< $(CMCLOBJS) $(ABSDYNLIB) $(FLINK) -o $@
	./$@
$(FE_DIR)/%f: $(FE_DIR)/%f.f $(CMCLOBJS) $(DYNLIB)
	$(FC) $(FFLAGS) $< $(CMCLOBJS) $(ABSDYNLIB) $(FLINK) -o $@
	./$@

fortran: $(FE)
# task always runs them (note escaped $ to pass to bash)...
	for i in $(FE); do echo $$i...; ./$$i; done
	@echo "Done running: $(FE)"


# matlab ----------------------------------------------------------------------
# matlab .mex* executable... (matlab is so slow to start, not worth testing it)
matlab: matlab/finufft.cpp $(STATICLIB)
	$(MEX) $< $(STATICLIB) $(INCL) $(MFLAGS) $(LIBSFFT) -output matlab/finufft

# octave .mex executable...
octave: matlab/finufft.cpp $(STATICLIB)
	(cd matlab; $(MKOCTFILE) --mex finufft.cpp -I../include ../$(STATICLIB) $(OFLAGS) $(LIBSFFT) -output finufft)
	@echo "Running octave interface tests; please wait a few seconds..."
	(cd matlab ;\
	$(OCTAVE) test/check_finufft.m ;\
	$(OCTAVE) test/check_finufft_single.m ;\
	$(OCTAVE) examples/guru1d1.m ;\
	$(OCTAVE) examples/guru1d1_single.m)

# for experts: force rebuilds fresh MEX (matlab/octave) gateway
# matlab/finufft.cpp via mwrap (needs recent version of mwrap >= 0.33.10)...
mex: matlab/finufft.mw
ifneq ($(MINGW),ON)
	(cd matlab ;\
	$(MWRAP) -mex finufft -c finufft.cpp -mb -cppcomplex finufft.mw)
else
	(cd matlab & $(MWRAP) -mex finufft -c finufft.cpp -mb -cppcomplex finufft.mw)
endif


# python ---------------------------------------------------------------------
python: $(STATICLIB) $(DYNLIB)
	FINUFFT_DIR=$(FINUFFT) $(PYTHON) -m pip -v install -e ./python
# note to devs: if trouble w/ NumPy, use: pip install ./python --no-deps
	$(PYTHON) python/test/run_accuracy_tests.py
	$(PYTHON) python/examples/simple1d1.py
	$(PYTHON) python/examples/simpleopts1d1.py
	$(PYTHON) python/examples/guru1d1.py
	$(PYTHON) python/examples/guru1d1f.py
	$(PYTHON) python/examples/simple2d1.py
	$(PYTHON) python/examples/many2d1.py
	$(PYTHON) python/examples/guru2d1.py
	$(PYTHON) python/examples/guru2d1f.py

# python packaging: *** please document these in make tasks echo above...
wheel: $(STATICLIB) $(DYNLIB)
	(export FINUFFT_DIR=$(shell pwd); cd python; $(PYTHON) -m pip wheel . -w wheelhouse; delocate-wheel -w fixed_wheel -v wheelhouse/finufft*.whl)

docker-wheel:
	docker run --rm -e package_name=finufft -v `pwd`:/io libinlu/manylinux2010_x86_64_fftw /io/python/ci/build-wheels.sh



# =============================== DOCUMENTATION =============================

docs: finufft-manual.pdf
finufft-manual.pdf: docs/conf.py docs/*.doc docs/*.sh docs/*.rst docs/tutorial/*.rst $(STATICLIB) $(DYNLIB) CHANGELOG docs/*.src
# also builds a local html for local browser check too...
	(cd docs; ./makecdocs.sh; make html && ./genpdfmanual.sh)
docs/matlabhelp.doc: docs/genmatlabhelp.sh matlab/*.sh matlab/*.docsrc matlab/*.docbit matlab/*.m
	(cd matlab; ./addmhelp.sh)
	(cd docs; ./genmatlabhelp.sh)



# =============================== CLEAN UP ==================================

clean: objclean pyclean
ifneq ($(MINGW),ON)
# non-Windows-WSL clean up...
	rm -f $(STATICLIB) $(DYNLIB)
	rm -f matlab/*.mex*
	rm -f $(TESTS) test/results/*.out perftest/results/*.out
	rm -f $(EXAMPLES) $(FE) $(ST) $(STF) $(GTT) $(GTTF)
	rm -f perftest/manysmallprobs
	rm -f examples/core test/core perftest/core $(FE_DIR)/core
else
# Windows-WSL clean up...
	del $(subst /,\,$(STATICLIB)), $(subst /,\,$(DYNLIB))
	del matlab\*.mex*
	for %%f in ($(subst /,\, $(TESTS))) do ((if exist %%f del %%f) & (if exist %%f.exe del %%f.exe))
	del test\results\*.out perftest\results\*.out
	for %%f in ($(subst /,\, $(EXAMPLES)), $(subst /,\,$(FE)), $(subst /,\,$(ST)), $(subst /,\,$(STF)), $(subst /,\,$(GTT)), $(subst /,\,$(GTTF))) do ((if exist %%f del %%f) & (if exist %%f.exe del %%f.exe))
	del perftest\manysmallprobs
	del examples\core, test\core, perftest\core, $(subst /,\, $(FE_DIR))\core
endif

# indiscriminate .o killer; needed before changing threading...
objclean:
ifneq ($(MINGW),ON)
# non-Windows-WSL...
	rm -f src/*.o test/directft/*.o test/*.o examples/*.o matlab/*.o contrib/*.o julia/*.o
	rm -f fortran/*.o $(FE_DIR)/*.o $(FD)/*.o
else
# Windows-WSL...
	for /d %%d in (src,test\directfttest,examples,matlab,contrib,julia) do (for %%f in (%%d\*.o) do (del %%f))
	for /d %%d in (fortran,$(subst /,\, $(FE_DIR)),$(subst /,\, $(FD))) do (for %%f in (%%d\*.o) do (del %%f))
endif

pyclean:
ifneq ($(MINGW),ON)
# non-Windows-WSL...
	rm -f python/finufft/*.pyc python/finufft/__pycache__/* python/test/*.pyc python/test/__pycache__/*
	rm -rf python/fixed_wheel python/wheelhouse
else
# Windows-WSL...
	for /d %%d in (python\finufft,python\test) do (for %%f in (%%d\*.pyc) do (del %%f))
	for /d %%d in (python\finufft\__pycache__,python\test\__pycache__) do (for %%f in (%%d\*) do (del %%f))
	for /d %%d in (python\fixed_wheel,python\wheelhouse) do (if exist %%d (rmdir /s /q %%d))
endif

# for experts; only run this if you possess mwrap to rebuild the interfaces!
mexclean:
ifneq ($(MINGW),ON)
# non-Windows-WSL...
	rm -f matlab/finufft_plan.m matlab/finufft.cpp matlab/finufft.mex*
else
# Windows-WSL...
	del matlab\finufft_plan.m matlab\finufft.cpp matlab\finufft.mex*
endif
