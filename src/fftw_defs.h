#ifndef FFTW_DEFS_H
#define FFTE_DEFS_H

// ---------- Precision-indep complex types, macros to FFTW -------------------

#include <fftw3.h>          // (after complex.h) needed so can typedef FFTW_CPX

// prec-indep interfaces to FFTW and other math utilities...
#ifdef SINGLE
  // single-prec, machine epsilon for rounding
  #define EPSILON (float)6e-08
  #define FABS(x) fabsf(x)
  typedef fftwf_complex FFTW_CPX;           //  single-prec has fftwf_*
  typedef fftwf_plan FFTW_PLAN;
  #define FFTW_INIT fftwf_init_threads
  #define FFTW_PLAN_TH fftwf_plan_with_nthreads
  #define FFTW_ALLOC_RE fftwf_alloc_real
  #define FFTW_ALLOC_CPX fftwf_alloc_complex
  #define FFTW_PLAN_1D fftwf_plan_dft_1d
  #define FFTW_PLAN_2D fftwf_plan_dft_2d
  #define FFTW_PLAN_3D fftwf_plan_dft_3d
  #define FFTW_PLAN_MANY_DFT fftwf_plan_many_dft
  #define FFTW_EX fftwf_execute
  #define FFTW_DE fftwf_destroy_plan
  #define FFTW_FR fftwf_free
  #define FFTW_FORGET_WISDOM fftwf_forget_wisdom
#else
  // double-precision, machine epsilon for rounding
  #define EPSILON (double)1.1e-16
  #define FABS(x) fabs(x)
  typedef fftw_complex FFTW_CPX;           // double-prec has fftw_*
  typedef fftw_plan FFTW_PLAN;
  #define FFTW_INIT fftw_init_threads
  #define FFTW_PLAN_TH fftw_plan_with_nthreads
  #define FFTW_ALLOC_RE fftw_alloc_real
  #define FFTW_ALLOC_CPX fftw_alloc_complex
  #define FFTW_PLAN_1D fftw_plan_dft_1d
  #define FFTW_PLAN_2D fftw_plan_dft_2d
  #define FFTW_PLAN_3D fftw_plan_dft_3d
  #define FFTW_PLAN_MANY_DFT fftw_plan_many_dft
  #define FFTW_EX fftw_execute
  #define FFTW_DE fftw_destroy_plan
  #define FFTW_FR fftw_free
  #define FFTW_FORGET_WISDOM fftw_forget_wisdom
#endif


#endif