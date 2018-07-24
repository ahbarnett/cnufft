#include "finufft.h"
#include "common.h"
#include <fftw3.h>
#include <math.h>
#include <stdio.h>
#include <iostream>
#include <iomanip>

int finufft2d1(BIGINT nj,FLT* xj,FLT *yj,CPX* cj,int iflag,
	       FLT eps, BIGINT ms, BIGINT mt, CPX* fk, nufft_opts opts)
 /*  Type-1 2D complex nonuniform FFT.

                  nj-1
     f[k1,k2] =   SUM  c[j] exp(+-i (k1 x[j] + k2 y[j]))
                  j=0

     for -ms/2 <= k1 <= (ms-1)/2,  -mt/2 <= k2 <= (mt-1)/2.

     The output array is k1 (fast), then k2 (slow), with each dimension
     determined by opts.modeord.
     If iflag>0 the + sign is used, otherwise the - sign is used,
     in the exponential.

   Inputs:
     nj     number of sources (int64)
     xj,yj     x,y locations of sources (each a size-nj FLT array) in [-3pi,3pi]
     cj     size-nj complex FLT array of source strengths,
            (ie, stored as 2*nj FLTs interleaving Re, Im).
     iflag  if >=0, uses + sign in exponential, otherwise - sign (int)
     eps    precision requested (>1e-16)
     ms,mt  number of Fourier modes requested in x and y (int64);
            each may be even or odd;
            in either case the mode range is integers lying in [-m/2, (m-1)/2]
     opts   struct controlling options (see finufft.h)
   Outputs:
     fk     complex FLT array of Fourier transform values
            (size ms*mt, fast in ms then slow in mt,
            ie Fortran ordering).
     returned value - 0 if success, else see ../docs/usage.rst

     The type 1 NUFFT proceeds in three main steps (see [GL]):
     1) spread data to oversampled regular mesh using kernel.
     2) compute FFT on uniform mesh
     3) deconvolve by division of each Fourier mode independently by the
        Fourier series coefficient of the kernel.
     The kernel coeffs are precomputed in what is called step 0 in the code.

   Written with FFTW style complex arrays. Barnett 2/1/17
 */
{
  spread_opts spopts;
  int ier_set = setup_spreader_for_nufft(spopts,eps,opts);
  if (ier_set) return ier_set;
  BIGINT nf1; set_nf_type12(ms,opts,spopts,&nf1);
  BIGINT nf2; set_nf_type12(mt,opts,spopts,&nf2);
  if (nf1*nf2>MAX_NF) {
    fprintf(stderr,"nf1*nf2=%.3g exceeds MAX_NF of %.3g\n",(double)nf1*nf2,(double)MAX_NF);
    return ERR_MAXNALLOC;
  }
  cout << scientific << setprecision(15);  // for debug

  if (opts.debug) printf("2d1: (ms,mt)=(%ld,%ld) (nf1,nf2)=(%ld,%ld) nj=%ld ...\n",(int64_t)ms,(int64_t)mt,(int64_t)nf1,(int64_t)nf2,(int64_t)nj);

  // STEP 0: get Fourier coeffs of spread kernel in each dim:
  CNTime timer; timer.start();
  FLT *fwkerhalf1 = (FLT*)malloc(sizeof(FLT)*(nf1/2+1));
  FLT *fwkerhalf2 = (FLT*)malloc(sizeof(FLT)*(nf2/2+1));
  onedim_fseries_kernel(nf1, fwkerhalf1, spopts);
  onedim_fseries_kernel(nf2, fwkerhalf2, spopts);
  if (opts.debug) printf("kernel fser (ns=%d):\t %.3g s\n", spopts.nspread,timer.elapsedsec());

  int nth = MY_OMP_GET_MAX_THREADS();
  if (nth>1) {             // set up multithreaded fftw stuff...
    FFTW_INIT();
    FFTW_PLAN_TH(nth);
  }
  timer.restart();
  FFTW_CPX *fw = FFTW_ALLOC_CPX(nf1*nf2);  // working upsampled array
  int fftsign = (iflag>=0) ? 1 : -1;
  FFTW_PLAN p = FFTW_PLAN_2D(nf2,nf1,fw,fw,fftsign, opts.fftw);  // in-place
  if (opts.debug) printf("fftw plan (%d)    \t %.3g s\n",opts.fftw,timer.elapsedsec());

  // Step 1: spread from irregular points to regular grid
  timer.restart();
  spopts.spread_direction = 1;
  FLT *dummy;
  int ier_spread = cnufftspread(nf1,nf2,1,(FLT*)fw,nj,xj,yj,dummy,(FLT*)cj,spopts);
  if (opts.debug) printf("spread (ier=%d):\t\t %.3g s\n",ier_spread,timer.elapsedsec());
  if (ier_spread>0) return ier_spread;

  // Step 2:  Call FFT
  timer.restart();
  FFTW_EX(p);
  FFTW_DE(p);
  if (opts.debug) printf("fft (%d threads):\t %.3g s\n", nth, timer.elapsedsec());

  // Step 3: Deconvolve by dividing coeffs by that of kernel; shuffle to output
  timer.restart();
  deconvolveshuffle2d(1,1.0,fwkerhalf1,fwkerhalf2,ms,mt,(FLT*)fk,nf1,nf2,fw,opts.modeord);
  if (opts.debug) printf("deconvolve & copy out:\t %.3g s\n", timer.elapsedsec());

  FFTW_FR(fw); free(fwkerhalf1); free(fwkerhalf2);
  if (opts.debug) printf("freed\n");
  return 0;
}

int finufft2d2(BIGINT nj,FLT* xj,FLT *yj,CPX* cj,int iflag,FLT eps,
	       BIGINT ms, BIGINT mt, CPX* fk, nufft_opts opts)

 /*  Type-2 2D complex nonuniform FFT.

     cj[j] =  SUM   fk[k1,k2] exp(+/-i (k1 xj[j] + k2 yj[j]))      for j = 0,...,nj-1
             k1,k2
     where sum is over -ms/2 <= k1 <= (ms-1)/2, -mt/2 <= k2 <= (mt-1)/2,

   Inputs:
     nj     number of sources (int64)
     xj,yj     x,y locations of sources (each a size-nj FLT array) in [-3pi,3pi]
     fk     FLT complex array of Fourier transform values (size ms*mt,
            increasing fast in ms then slow in mt, ie Fortran ordering).
            Along each dimension the ordering is set by opts.modeord.
     iflag  if >=0, uses + sign in exponential, otherwise - sign (int)
     eps    precision requested (>1e-16)
     ms,mt  numbers of Fourier modes given in x and y (int64)
            each may be even or odd;
            in either case the mode range is integers lying in [-m/2, (m-1)/2].
     opts   struct controlling options (see finufft.h)
   Outputs:
     cj     size-nj complex FLT array of target values
            (ie, stored as 2*nj FLTs interleaving Re, Im).
     returned value - 0 if success, else see ../docs/usage.rst

     The type 2 algorithm proceeds in three main steps (see [GL]).
     1) deconvolve (amplify) each Fourier mode, dividing by kernel Fourier coeff
     2) compute inverse FFT on uniform fine grid
     3) spread (dir=2, ie interpolate) data to regular mesh
     The kernel coeffs are precomputed in what is called step 0 in the code.

   Written with FFTW style complex arrays. Barnett 2/1/17
 */
{
  spread_opts spopts;
  int ier_set = setup_spreader_for_nufft(spopts,eps,opts);
  if (ier_set) return ier_set;
  BIGINT nf1; set_nf_type12(ms,opts,spopts,&nf1);
  BIGINT nf2; set_nf_type12(mt,opts,spopts,&nf2);
  if (nf1*nf2>MAX_NF) {
    fprintf(stderr,"nf1*nf2=%.3g exceeds MAX_NF of %.3g\n",(double)nf1*nf2,(double)MAX_NF);
    return ERR_MAXNALLOC;
  }
  cout << scientific << setprecision(15);  // for debug

  if (opts.debug) printf("2d2: (ms,mt)=(%ld,%ld) (nf1,nf2)=(%ld,%ld) nj=%ld ...\n",(int64_t)ms,(int64_t)mt,(int64_t)nf1,(int64_t)nf2,(int64_t)nj);

  // STEP 0: get Fourier coeffs of spread kernel in each dim:
  CNTime timer; timer.start();
  FLT *fwkerhalf1 = (FLT*)malloc(sizeof(FLT)*(nf1/2+1));
  FLT *fwkerhalf2 = (FLT*)malloc(sizeof(FLT)*(nf2/2+1));
  onedim_fseries_kernel(nf1, fwkerhalf1, spopts);
  onedim_fseries_kernel(nf2, fwkerhalf2, spopts);
  if (opts.debug) printf("kernel fser (ns=%d):\t %.3g s\n", spopts.nspread,timer.elapsedsec());

  int nth = MY_OMP_GET_MAX_THREADS();
  if (nth>1) {             // set up multithreaded fftw stuff...
    FFTW_INIT();
    FFTW_PLAN_TH(nth);
  }
  timer.restart();
  FFTW_CPX *fw = FFTW_ALLOC_CPX(nf1*nf2);  // working upsampled array
  int fftsign = (iflag>=0) ? 1 : -1;
  FFTW_PLAN p = FFTW_PLAN_2D(nf2,nf1,fw,fw,fftsign, opts.fftw);  // in-place
  if (opts.debug) printf("fftw plan (%d)    \t %.3g s\n",opts.fftw,timer.elapsedsec());

  // STEP 1: amplify Fourier coeffs fk and copy into upsampled array fw
  timer.restart();
  deconvolveshuffle2d(2,1.0,fwkerhalf1,fwkerhalf2,ms,mt,(FLT*)fk,nf1,nf2,fw,opts.modeord);
  if (opts.debug) printf("amplify & copy in:\t %.3g s\n",timer.elapsedsec());
  //cout<<"fw:\n"; for (int j=0;j<nf1*nf2;++j) cout<<fw[j][0]<<"\t"<<fw[j][1]<<endl;

  // Step 2:  Call FFT
  timer.restart();
  FFTW_EX(p);
  FFTW_DE(p);
  if (opts.debug) printf("fft (%d threads):\t %.3g s\n",nth,timer.elapsedsec());

  // Step 3: unspread (interpolate) from regular to irregular target pts
  timer.restart();
  spopts.spread_direction = 2;
  FLT *dummy;
  int ier_spread = cnufftspread(nf1,nf2,1,(FLT*)fw,nj,xj,yj,dummy,(FLT*)cj,spopts);
  if (opts.debug) printf("unspread (ier=%d):\t %.3g s\n",ier_spread,timer.elapsedsec());
  if (ier_spread>0) return ier_spread;

  FFTW_FR(fw); free(fwkerhalf1); free(fwkerhalf2);
  if (opts.debug) printf("freed\n");
  return 0;
}

int finufft2d3(BIGINT nj,FLT* xj,FLT* yj,CPX* cj,int iflag, FLT eps, BIGINT nk, FLT* s, FLT *t, CPX* fk, nufft_opts opts)
 /*  Type-3 2D complex nonuniform FFT.

               nj-1
     fk[k]  =  SUM   c[j] exp(+-i (s[k] xj[j] + t[k] yj[j]),    for k=0,...,nk-1
               j=0
   Inputs:
     nj     number of sources (int64)
     xj,yj  x,y location of sources in the plane R^2 (each size-nj FLT array)
     cj     size-nj complex FLT array of source strengths,
            (ie, stored as 2*nj FLTs interleaving Re, Im).
     nk     number of frequency target points (int64)
     s,t    (k_x,k_y) frequency locations of targets in R^2.
     iflag  if >=0, uses + sign in exponential, otherwise - sign (int)
     eps    precision requested (>1e-16)
     opts   struct controlling options (see finufft.h)
   Outputs:
     fk     size-nk complex FLT Fourier transform values at the
            target frequencies sk
     returned value - 0 if success, else see ../docs/usage.rst

     The type 3 algorithm is basically a type 2 (which is implemented precisely
     as call to type 2) replacing the middle FFT (Step 2) of a type 1. See [LG].
     Beyond this, the new twists are:
     i) number of upsampled points for the type-1 in each dim, depends on the
       product of interval widths containing input and output points (X*S), for
       that dim.
     ii) The deconvolve (post-amplify) step is division by the Fourier transform
       of the scaled kernel, evaluated on the *nonuniform* output frequency
       grid; this is done by direct approximation of the Fourier integral
       using quadrature of the kernel function times exponentials.
     iii) Shifts in x (real) and s (Fourier) are done to minimize the interval
       half-widths X and S, hence nf, in each dim.

   No references to FFTW are needed here. Some CPX arithmetic is used,
   thus compile with -Ofast in GNU.
   Barnett 2/17/17, 6/12/17
 */
{
  spread_opts spopts;
  int ier_set = setup_spreader_for_nufft(spopts,eps,opts);
  if (ier_set) return ier_set;
  BIGINT nf1,nf2;
  FLT X1,C1,S1,D1,h1,gam1,X2,C2,S2,D2,h2,gam2;
  cout << scientific << setprecision(15);  // for debug

  // pick x, s intervals & shifts, then apply these to xj, cj (twist iii)...
  CNTime timer; timer.start();
  arraywidcen(nj,xj,&X1,&C1);  // get half-width, center, containing {x_j}
  arraywidcen(nk,s,&S1,&D1);   // {s_k}
  arraywidcen(nj,yj,&X2,&C2);  // {y_j}
  arraywidcen(nk,t,&S2,&D2);   // {t_k}
  set_nhg_type3(S1,X1,opts,spopts,&nf1,&h1,&gam1);          // applies twist i)
  set_nhg_type3(S2,X2,opts,spopts,&nf2,&h2,&gam2);
  if (opts.debug) printf("2d3: X1=%.3g C1=%.3g S1=%.3g D1=%.3g gam1=%g nf1=%ld X2=%.3g C2=%.3g S2=%.3g D2=%.3g gam2=%g nf2=%ld nj=%ld nk=%ld...\n",X1,C1,S1,D1,gam1,(int64_t)nf1,X2,C2,S2,D2,gam2,(int64_t)nf2,(int64_t)nj,(int64_t)nk);
  if ((int64_t)nf1*nf2>MAX_NF) {
    fprintf(stderr,"nf1*nf2=%.3g exceeds MAX_NF of %.3g\n",(double)nf1*nf2,(double)MAX_NF);
    return ERR_MAXNALLOC;
  }
  FLT* xpj = (FLT*)malloc(sizeof(FLT)*nj);
  FLT* ypj = (FLT*)malloc(sizeof(FLT)*nj);
  for (BIGINT j=0;j<nj;++j) {
    xpj[j] = (xj[j]-C1) / gam1;          // rescale x_j
    ypj[j] = (yj[j]-C2) / gam2;          // rescale y_j
  }
  CPX imasign = (iflag>=0) ? IMA : -IMA;
  CPX* cpj = (CPX*)malloc(sizeof(CPX)*nj);  // c'_j rephased src
  if (D1!=0.0 || D2!=0.0) {
#pragma omp parallel for schedule(dynamic)               // since cexp slow
    for (BIGINT j=0;j<nj;++j)
      cpj[j] = cj[j] * exp(imasign*(D1*xj[j]+D2*yj[j])); // rephase c_j -> c'_j
    if (opts.debug) printf("prephase:\t\t %.3g s\n",timer.elapsedsec());
  } else
    for (BIGINT j=0;j<nj;++j)
      cpj[j] = cj[j];                                    // just copy over

  // Step 1: spread from irregular sources to regular grid as in type 1
  CPX* fw = (CPX*)malloc(sizeof(CPX)*nf1*nf2);
  timer.restart();
  spopts.spread_direction = 1;
  FLT *dummy;
  int ier_spread = cnufftspread(nf1,nf2,1,(FLT*)fw,nj,xpj,ypj,dummy,(FLT*)cpj,spopts);
  free(xpj); free(ypj); free(cpj);
  if (opts.debug) printf("spread (ier=%d):\t\t %.3g s\n",ier_spread,timer.elapsedsec());
  if (ier_spread>0) return ier_spread;

  // Step 2: call type-2 to eval regular as Fourier series at rescaled targs
  timer.restart();
  FLT *sp = (FLT*)malloc(sizeof(FLT)*nk);     // rescaled targs s'_k
  FLT *tp = (FLT*)malloc(sizeof(FLT)*nk);     // t'_k
  for (BIGINT k=0;k<nk;++k) {
    sp[k] = h1*gam1*(s[k]-D1);                         // so that |s'_k| < pi/R
    tp[k] = h2*gam2*(t[k]-D2);                         // so that |t'_k| < pi/R
  }
  int ier_t2 = finufft2d2(nk,sp,tp,fk,iflag,eps,nf1,nf2,fw,opts);
  free(fw);
  if (opts.debug) printf("total type-2 (ier=%d):\t %.3g s\n",ier_t2,timer.elapsedsec());
  if (ier_t2) exit(ier_t2);

  // Step 3a: compute Fourier transform of scaled kernel at targets
  timer.restart();
  FLT *fkker1 = (FLT*)malloc(sizeof(FLT)*nk);
  FLT *fkker2 = (FLT*)malloc(sizeof(FLT)*nk);
  // exploit that Fourier transform separates because kernel built separable...
  onedim_nuft_kernel(nk, sp, fkker1, spopts);           // fill fkker1
  onedim_nuft_kernel(nk, tp, fkker2, spopts);           // fill fkker2
  if (opts.debug) printf("kernel FT (ns=%d):\t %.3g s\n", spopts.nspread,timer.elapsedsec());
  free(sp); free(tp);
  // Step 3b: correct for spreading by dividing by the Fourier transform from 3a
  timer.restart();
  if (isfinite(C1) && isfinite(C2) && (C1!=0.0 || C2!=0.0))
#pragma omp parallel for schedule(dynamic)              // since cexps slow
    for (BIGINT k=0;k<nk;++k)         // also phases to account for C1,C2 shift
      fk[k] *= (CPX)(1.0/(fkker1[k]*fkker2[k])) *
	exp(imasign*((s[k]-D1)*C1 + (t[k]-D2)*C2));
  else
#pragma omp parallel for schedule(dynamic)
    for (BIGINT k=0;k<nk;++k)         // also phases to account for C1,C2 shift
      fk[k] *= (CPX)(1.0/(fkker1[k]*fkker2[k]));
  if (opts.debug) printf("deconvolve:\t\t %.3g s\n",timer.elapsedsec());

  free(fkker1); free(fkker2); if (opts.debug) printf("freed\n");
  return 0;
}

#if 0
// Codes for 2d1many
FFTW_PLAN finufft2d1plan(BIGINT n1, BIGINT n2, FFTW_CPX* in, int fftsign, nufft_opts opts, int nth)
{
  if (nth>1) {             // set up multithreaded fftw stuff...
    FFTW_INIT();
    FFTW_PLAN_TH(nth);
  }
  CNTime timer; timer.start();
  FFTW_PLAN p = FFTW_PLAN_2D(n2,n1,in,in,fftsign,opts.fftw);  // in-place
  if (opts.debug) printf("fftw plan (%d)    \t %.3g s\n",opts.fftw,timer.elapsedsec());

  return p;
}

void finufft2d1execute(FFTW_PLAN p)
{
  FFTW_EX(p);
}

void finufft2d1destroy(FFTW_PLAN p)
{
  FFTW_DE(p);
  fftw_cleanup();
}
#endif

static int finufft2d1manyseq(int ndata, BIGINT nj, FLT* xj, FLT *yj, CPX* c,
                             int iflag, FLT eps, BIGINT ms, BIGINT mt, CPX* fk,
                             nufft_opts opts)
{
  spread_opts spopts;
  int ier_set = setup_spreader_for_nufft(spopts,eps,opts);
  if (ier_set) return ier_set;
  BIGINT nf1; set_nf_type12((BIGINT)ms,opts,spopts,&nf1);
  BIGINT nf2; set_nf_type12((BIGINT)mt,opts,spopts,&nf2);
  if (nf1*nf2>MAX_NF) {
    fprintf(stderr,"nf1*nf2=%.3g exceeds MAX_NF of %.3g\n",(double)nf1*nf2,(double)MAX_NF);
    return ERR_MAXNALLOC;
  }
  cout << scientific << setprecision(15);  // for debug

  if (opts.debug) printf("2d1: (ms,mt)=(%ld,%ld) (nf1,nf2)=(%ld,%ld) nj=%ld ...\n",
                         (BIGINT)ms,(BIGINT)mt,nf1,nf2,(BIGINT)nj);

  // STEP 0: get Fourier coeffs of spread kernel in each dim:
  CNTime timer; timer.start();
  FLT *fwkerhalf1 = (FLT*)malloc(sizeof(FLT)*(nf1/2+1));
  FLT *fwkerhalf2 = (FLT*)malloc(sizeof(FLT)*(nf2/2+1));
  onedim_fseries_kernel(nf1, fwkerhalf1, spopts);
  onedim_fseries_kernel(nf2, fwkerhalf2, spopts);
  if (opts.debug) printf("kernel fser (ns=%d):\t %.3g s\n", spopts.nspread,timer.elapsedsec());

  int nth = MY_OMP_GET_MAX_THREADS();
  if (nth>1) {             // set up multithreaded fftw stuff...
    FFTW_INIT();
    FFTW_PLAN_TH(nth);
  }
  timer.restart();
  FFTW_CPX *fw = FFTW_ALLOC_CPX(nf1*nf2);  // working upsampled array
  int fftsign = (iflag>=0) ? 1 : -1;
  FFTW_PLAN p = FFTW_PLAN_2D(nf2,nf1,fw,fw,fftsign, opts.fftw);  // in-place
  if (opts.debug) printf("fftw plan (%d)    \t %.3g s\n",opts.fftw,timer.elapsedsec());

  spopts.debug = opts.spread_debug;
  spopts.sort = opts.spread_sort;
  spopts.spread_direction = 1;
  spopts.pirange = 1; FLT *dummy;
  spopts.chkbnds = opts.chkbnds;

  BIGINT* sort_indices = (BIGINT*)malloc(sizeof(BIGINT)*nj);;
  int ier_spread = cnufftcheck(nf1,nf2,1,nj,xj,yj,dummy,spopts);
  int did_sort = cnufftsort(sort_indices,nf1,nf2,1,nj,xj,yj,dummy,spopts);
  if (ier_spread>0) return ier_spread;

  double time_fft = 0.0, time_spread = 0.0, time_deconv = 0.0;

  for (int i = 0; i < ndata; ++i)
  {
    CPX* cstart = c+i*nj;
    CPX* fkstart = fk+i*ms*mt;

    // Step 1: spread from irregular points to regular grid
    timer.restart();
    ier_spread = cnufftspreadwithsortidx(sort_indices,nf1,nf2,1,(FLT*)fw,nj,xj,
                                         yj,dummy,(FLT*)cstart,spopts,did_sort);
    if (ier_spread>0) return ier_spread;
    time_spread+=timer.elapsedsec();
    // if (opts.debug) printf("spread (ier=%d):\t\t %.3g s\n",ier_spread,timer.elapsedsec());

    // Step 2:  Call FFT
    timer.restart();
    FFTW_EX(p);
    time_fft+=timer.elapsedsec();
    // if (opts.debug) printf("fft (%d threads):\t %.3g s\n", nth, timer.elapsedsec());

    // Step 3: Deconvolve by dividing coeffs by that of kernel; shuffle to output
    timer.restart();
    deconvolveshuffle2d(1,1.0,fwkerhalf1,fwkerhalf2,ms,mt,(FLT*)fkstart,nf1,nf2,fw,opts.modeord);
    time_deconv+=timer.elapsedsec();
    // if (opts.debug) printf("deconvolve & copy out:\t %.3g s\n", timer.elapsedsec());
  }
  if (opts.debug) printf("[manyseq] spread (ier=%d):\t\t %.3g s\n", 0,time_spread);
  if (opts.debug) printf("[manyseq] fft (%d threads):\t\t %.3g s\n", nth, time_fft);
  if (opts.debug) printf("[manyseq] deconvolve & copy out:\t %.3g s\n", time_deconv);

  if (opts.debug) printf("[manyseq] total execute time (exclude fftw_plan, etc.) %.3g s\n",
                         time_spread+time_fft+time_deconv);

  FFTW_DE(p);
  FFTW_FR(fw); free(fwkerhalf1); free(fwkerhalf2); free(sort_indices);
  if (opts.debug) printf("freed\n");
  return 0;
}

static int finufft2d1manysimul(int ndata, BIGINT nj, FLT* xj, FLT *yj, CPX* c,
                               int iflag, FLT eps, BIGINT ms, BIGINT mt, CPX* fk,
                               nufft_opts opts)
{
  spread_opts spopts;
  int ier_set = setup_spreader_for_nufft(spopts,eps,opts);
  if (ier_set) return ier_set;
  BIGINT nf1; set_nf_type12((BIGINT)ms,opts,spopts,&nf1);
  BIGINT nf2; set_nf_type12((BIGINT)mt,opts,spopts,&nf2);
  if (nf1*nf2>MAX_NF) {
    fprintf(stderr,"nf1*nf2=%.3g exceeds MAX_NF of %.3g\n",(double)nf1*nf2,(double)MAX_NF);
    return ERR_MAXNALLOC;
  }
  cout << scientific << setprecision(15);  // for debug

  if (opts.debug) printf("2d1: (ms,mt)=(%ld,%ld) (nf1,nf2)=(%ld,%ld) nj=%ld ...\n",
                         (BIGINT)ms,(BIGINT)mt,nf1,nf2,(BIGINT)nj);

  // STEP 0: get Fourier coeffs of spread kernel in each dim:
  CNTime timer; timer.start();
  FLT *fwkerhalf1 = (FLT*)malloc(sizeof(FLT)*(nf1/2+1));
  FLT *fwkerhalf2 = (FLT*)malloc(sizeof(FLT)*(nf2/2+1));
  onedim_fseries_kernel(nf1, fwkerhalf1, spopts);
  onedim_fseries_kernel(nf2, fwkerhalf2, spopts);
  if (opts.debug) printf("kernel fser (ns=%d):\t %.3g s\n", spopts.nspread,timer.elapsedsec());

  int nth = MY_OMP_GET_MAX_THREADS();
  if (nth>1) {             // set up multithreaded fftw stuff...
    FFTW_INIT();
    FFTW_PLAN_TH(nth);
  }

  FFTW_CPX *fw = FFTW_ALLOC_CPX(nf1*nf2*nth);  // working upsampled array
  int fftsign = (iflag>=0) ? 1 : -1;
  const int n[] = {int(nf2), int(nf1)};
  // http://www.fftw.org/fftw3_doc/Row_002dmajor-Format.html#Row_002dmajor-Format

  timer.restart();
  FFTW_PLAN p = fftw_plan_many_dft(2, n, nth, fw, n, 1, n[0]*n[1], fw, n, 1,
                                   n[0]*n[1], fftsign, opts.fftw);
  if (opts.debug) printf("fftw plan (%d)    \t %.3g s\n",opts.fftw,timer.elapsedsec());

  spopts.debug = opts.spread_debug;
  spopts.sort = opts.spread_sort;
  spopts.spread_direction = 1;
  spopts.pirange = 1; FLT *dummy;
  spopts.chkbnds = opts.chkbnds;

  BIGINT* sort_indices = (BIGINT*)malloc(sizeof(BIGINT)*nj);
  int ier_spread = cnufftcheck(nf1,nf2,1,nj,xj,yj,dummy,spopts);
  int did_sort = cnufftsort(sort_indices,nf1,nf2,1,nj,xj,yj,dummy,spopts);
  if (ier_spread>0) return ier_spread;

  double time_fft = 0.0, time_spread = 0.0, time_deconv = 0.0;
  CPX* cstart;
  CPX* fkstart;
  FFTW_CPX *fwstart;

  int ier_spreads[nth] = {0}; // Since we can't do return in openmp, we need to
                              // have these and check the error after exit the omp block
  int abort[nth] = {0};

#if _OPENMP
  omp_set_nested(0);// to make sure only single thread are executing cnufftspread
                    // for each data
#endif

  for (int j = 0; j*nth < ndata; ++j) // here, assume ndata is multiple of nth
  {
    timer.restart();
    // Step 1: spread from irregular points to regular grid
    #pragma omp parallel for private(ier_spread,cstart,fwstart)
    for (int i = 0; i<min(ndata-j*nth,nth); ++i)
    {
      cstart  = c + (i+j*nth)*nj;
      fwstart = fw + i*nf1*nf2;
      ier_spread = cnufftspreadwithsortidx(sort_indices,nf1,nf2,1,(FLT*)fwstart,
                                          nj,xj,yj,dummy,(FLT*)cstart,spopts,did_sort);
      if (ier_spread>0) {
        ier_spreads[i] = ier_spread;
        abort[i] = 1;
      }
    }
    time_spread+=timer.elapsedsec();
    // if (opts.debug) printf("spread (ier=%d):\t\t %.3g s\n",ier_spread,timer.elapsedsec());

    for (int i = 0; i < nth; ++i)
    {
      if (abort[i])
      {
        return ier_spreads[i];
      }
    }

    // Step 2:  Call FFT
    timer.restart();
    FFTW_EX(p);
    time_fft+=timer.elapsedsec();
    // if (opts.debug) printf("fft (%d threads):\t %.3g s\n", nth, timer.elapsedsec());

    // Step 3: Deconvolve by dividing coeffs by that of kernel; shuffle to output
    timer.restart();
    #pragma omp parallel for private(fkstart, fwstart)
      for (int i = 0; i <min(ndata-j*nth, nth); ++i)
      {
        fkstart = fk + (i+j*nth)*ms*mt;
        fwstart = fw + i*nf1*nf2;
        deconvolveshuffle2d(1,1.0,fwkerhalf1,fwkerhalf2,ms,mt,(FLT*)fkstart,nf1,
                            nf2,fwstart,opts.modeord);
      }
    time_deconv+=timer.elapsedsec();
    // if (opts.debug) printf("deconvolve & copy out:\t %.3g s\n", timer.elapsedsec());

  }

  if (opts.debug) printf("[manysimul] spread (ier=%d):\t\t %.3g s\n", 0,time_spread);
  if (opts.debug) printf("[manysimul] fft (%d threads):\t\t %.3g s\n", nth, time_fft);
  if (opts.debug) printf("[manysimul] deconvolve & copy out:\t %.3g s\n", time_deconv);

  if (opts.debug) printf("[manysimul] total execute time (exclude fftw_plan, etc.) %.3g s\n",
                         time_spread+time_fft+time_deconv);

  FFTW_DE(p);
  FFTW_FR(fw); free(fwkerhalf1); free(fwkerhalf2); free(sort_indices);
  if (opts.debug) printf("freed\n");
  return 0;
}

int finufft2d1many(int ndata, BIGINT nj, FLT* xj, FLT *yj, CPX* c, int iflag,
                   FLT eps, BIGINT ms, BIGINT mt, CPX* fk, nufft_opts opts)
/*
  Type-1 2D complex nonuniform FFT for multiple data.

                    nj
    f[k1,k2,d] =   SUM  c[j,d] exp(+-i (k1 x[j] + k2 y[j]))
                   j=1

    for -ms/2 <= k1 <= (ms-1)/2, -mt/2 <= k2 <= (mt-1)/2, d = 0, ..., ndata-1

    The output array is in increasing k1 ordering (fast), then increasing
    k2 ordering (slow), then increasing d (slowest). If iflag>0 the + sign
    is used, otherwise the - sign is used, in the exponential.
  Inputs:
    ndata  number of data
    nj     number of sources
    xj,yj  x,y locations of sources on 2D domain [-pi,pi]^2.
    c      a size nj*ndata complex FLT array of source strengths,
           increasing fast in nj then slow in ndata.
    iflag  if >=0, uses + sign in exponential, otherwise - sign.
    eps    precision requested (>1e-16)
    ms,mt  number of Fourier modes requested in x and y; each may be even or odd;
           in either case the mode range is integers lying in [-m/2, (m-1)/2]
    opts   struct controlling options (see finufft.h)
  Outputs:
    fk     complex FLT array of Fourier transform values
           (size ms*mt*ndata, increasing fast in ms then slow in mt then in ndata
           ie Fortran ordering).
    returned value - 0 if success, else see ../docs/usage.rst

  The type 1 NUFFT proceeds in three main steps (see [GL]):
  1) spread data to oversampled regular mesh using kernel.
  2) compute FFT on uniform mesh
  3) deconvolve by division of each Fourier mode independently by the
     Fourier series coefficient of the kernel.
  The kernel coeffs are precomputed in what is called step 0 in the code.
 */
{
  if (ndata<1) {        // factor is since fortran wants 1e-16 to be ok
    fprintf(stderr,"ndata should be at least 1 (ndata=%d)\n",ndata);
    return ERR_NDATA_NOTVALID;
  }
  if (opts.many_seq){
    return finufft2d1manyseq(ndata,nj,xj,yj,c,iflag,eps,ms,mt,fk,opts);
  }
  else{
    return finufft2d1manysimul(ndata,nj,xj,yj,c,iflag,eps,ms,mt,fk,opts);
  }
}

static int finufft2d2manyseq(int ndata, BIGINT nj, FLT* xj, FLT *yj, CPX* c, int iflag,
                             FLT eps, BIGINT ms, BIGINT mt, CPX* fk, nufft_opts opts)
{
  spread_opts spopts;
  int ier_set = setup_spreader_for_nufft(spopts,eps,opts);
  if (ier_set) return ier_set;
  BIGINT nf1; set_nf_type12(ms,opts,spopts,&nf1);
  BIGINT nf2; set_nf_type12(mt,opts,spopts,&nf2);
  if (nf1*nf2>MAX_NF) {
    fprintf(stderr,"nf1*nf2=%.3g exceeds MAX_NF of %.3g\n",(double)nf1*nf2,(double)MAX_NF);
    return ERR_MAXNALLOC;
  }
  cout << scientific << setprecision(15);  // for debug

  if (opts.debug) printf("2d2: (ms,mt)=(%ld,%ld) (nf1,nf2)=(%ld,%ld) nj=%ld ...\n",
                          (int64_t)ms,(int64_t)mt,(int64_t)nf1,(int64_t)nf2,
                          (int64_t)nj);

  // STEP 0: get Fourier coeffs of spread kernel in each dim:
  CNTime timer; timer.start();
  FLT *fwkerhalf1 = (FLT*)malloc(sizeof(FLT)*(nf1/2+1));
  FLT *fwkerhalf2 = (FLT*)malloc(sizeof(FLT)*(nf2/2+1));
  onedim_fseries_kernel(nf1, fwkerhalf1, spopts);
  onedim_fseries_kernel(nf2, fwkerhalf2, spopts);
  if (opts.debug) printf("kernel fser (ns=%d):\t %.3g s\n", spopts.nspread,timer.elapsedsec());


  int nth = MY_OMP_GET_MAX_THREADS();
  if (nth>1) {             // set up multithreaded fftw stuff...
    FFTW_INIT();
    FFTW_PLAN_TH(nth);
  }
  timer.restart();
  FFTW_CPX *fw = FFTW_ALLOC_CPX(nf1*nf2);  // working upsampled array
  int fftsign = (iflag>=0) ? 1 : -1;
  FFTW_PLAN p = FFTW_PLAN_2D(nf2,nf1,fw,fw,fftsign, opts.fftw);  // in-place
  if (opts.debug) printf("fftw plan (%d)    \t %.3g s\n",opts.fftw,timer.elapsedsec());

  spopts.debug = opts.spread_debug;
  spopts.sort = opts.spread_sort;
  spopts.spread_direction = 2;
  spopts.pirange = 1; FLT *dummy;
  spopts.chkbnds = opts.chkbnds;

  BIGINT* sort_indices = (BIGINT*)malloc(sizeof(BIGINT)*nj);;
  int ier_spread = cnufftcheck(nf1,nf2,1,nj,xj,yj,dummy,spopts);
  int did_sort = cnufftsort(sort_indices,nf1,nf2,1,nj,xj,yj,dummy,spopts);
  if (ier_spread>0) return ier_spread;

  double time_fft = 0.0, time_spread = 0.0, time_deconv = 0.0;

  for (int i = 0; i < ndata; ++i)
  {
    CPX* cstart = c+i*nj;
    CPX* fkstart = fk+i*ms*mt;

    // STEP 1: amplify Fourier coeffs fk and copy into upsampled array fw
    timer.restart();
    deconvolveshuffle2d(2,1.0,fwkerhalf1,fwkerhalf2,ms,mt,(FLT*)fkstart,nf1,nf2,fw,opts.modeord);
    time_deconv+=timer.elapsedsec();
    // if (opts.debug) printf("amplify & copy in:\t %.3g s\n",timer.elapsedsec());
    //cout<<"fw:\n"; for (int j=0;j<nf1*nf2;++j) cout<<fw[j][0]<<"\t"<<fw[j][1]<<endl;

    // Step 2:  Call FFT
    timer.restart();
    FFTW_EX(p);
    time_fft+=timer.elapsedsec();
    // if (opts.debug) printf("fft (%d threads):\t %.3g s\n",nth,timer.elapsedsec());

    // Step 3: unspread (interpolate) from regular to irregular target pts
    timer.restart();
    ier_spread = cnufftspreadwithsortidx(sort_indices,nf1,nf2,1,(FLT*)fw,nj,xj,yj,
                                         dummy,(FLT*)cstart,spopts,did_sort);
    if (ier_spread>0) return ier_spread;
    time_spread+=timer.elapsedsec();
    // if (opts.debug) printf("unspread (ier=%d):\t %.3g s\n",ier_spread,timer.elapsedsec());
  }
  if (opts.debug) printf("[manyseq] amplify & copy in:\t %.3g s\n", time_deconv);
  if (opts.debug) printf("[manyseq] fft (%d threads):\t\t %.3g s\n", nth, time_fft);
  if (opts.debug) printf("[manyseq] unspread (ier=%d):\t\t %.3g s\n", 0,time_spread);

  if (opts.debug) printf("[manyseq] total execute time (exclude fftw_plan, etc.) %.3g s\n",
                         time_spread+time_fft+time_deconv);

  FFTW_FR(fw); free(fwkerhalf1); free(fwkerhalf2); free(sort_indices);
  if (opts.debug) printf("freed\n");
  return 0;
}

static int finufft2d2manysimul(int ndata, BIGINT nj, FLT* xj, FLT *yj, CPX* c, int iflag,
                              FLT eps, BIGINT ms, BIGINT mt, CPX* fk, nufft_opts opts)
{
  spread_opts spopts;
  int ier_set = setup_spreader_for_nufft(spopts,eps,opts);
  if (ier_set) return ier_set;
  BIGINT nf1; set_nf_type12(ms,opts,spopts,&nf1);
  BIGINT nf2; set_nf_type12(mt,opts,spopts,&nf2);
  if (nf1*nf2>MAX_NF) {
    fprintf(stderr,"nf1*nf2=%.3g exceeds MAX_NF of %.3g\n",(double)nf1*nf2,(double)MAX_NF);
    return ERR_MAXNALLOC;
  }
  cout << scientific << setprecision(15);  // for debug

  if (opts.debug) printf("2d2: (ms,mt)=(%ld,%ld) (nf1,nf2)=(%ld,%ld) nj=%ld ...\n",
                         (int64_t)ms,(int64_t)mt,(int64_t)nf1,(int64_t)nf2,(int64_t)nj);

  // STEP 0: get Fourier coeffs of spread kernel in each dim:
  CNTime timer; timer.start();
  FLT *fwkerhalf1 = (FLT*)malloc(sizeof(FLT)*(nf1/2+1));
  FLT *fwkerhalf2 = (FLT*)malloc(sizeof(FLT)*(nf2/2+1));
  onedim_fseries_kernel(nf1, fwkerhalf1, spopts);
  onedim_fseries_kernel(nf2, fwkerhalf2, spopts);
  if (opts.debug) printf("kernel fser (ns=%d):\t %.3g s\n", spopts.nspread,timer.elapsedsec());


  int nth = MY_OMP_GET_MAX_THREADS();
  if (nth>1) {             // set up multithreaded fftw stuff...
    FFTW_INIT();
    FFTW_PLAN_TH(nth);
  }

  FFTW_CPX *fw = FFTW_ALLOC_CPX(nf1*nf2*nth);  // working upsampled array
  int fftsign = (iflag>=0) ? 1 : -1;
  const int n[] = {int(nf2), int(nf1)};
  // http://www.fftw.org/fftw3_doc/Row_002dmajor-Format.html#Row_002dmajor-Format

  timer.restart();
  FFTW_PLAN p = fftw_plan_many_dft(2, n, nth, fw, n, 1, n[0]*n[1], fw, n, 1,
                                   n[0]*n[1], fftsign, opts.fftw);
  if (opts.debug) printf("fftw plan (%d)    \t %.3g s\n",opts.fftw,timer.elapsedsec());

  spopts.debug = opts.spread_debug;
  spopts.sort = opts.spread_sort;
  spopts.spread_direction = 2;
  spopts.pirange = 1; FLT *dummy;
  spopts.chkbnds = opts.chkbnds;

  BIGINT* sort_indices = (BIGINT*)malloc(sizeof(BIGINT)*nj);;
  int ier_spread = cnufftcheck(nf1,nf2,1,nj,xj,yj,dummy,spopts);
  int did_sort = cnufftsort(sort_indices,nf1,nf2,1,nj,xj,yj,dummy,spopts);
  if (ier_spread>0) return ier_spread;

  double time_fft = 0.0, time_spread = 0.0, time_deconv = 0.0;
  CPX* cstart;
  CPX* fkstart;
  FFTW_CPX *fwstart;

  int ier_spreads[nth] = {0}; // Since we can't do return in openmp, we need to have
                              // these and check the error after exit the omp block
  int abort[nth] = {0};

#if _OPENMP
  omp_set_nested(0);// to make sure only single thread are executing cnufftspread for each data
#endif
  for (int j = 0; j*nth < ndata; ++j)
  {
    // STEP 1: amplify Fourier coeffs fk and copy into upsampled array fw
    timer.restart();
    #pragma omp parallel for private(fkstart, fwstart)
      for (int i = 0; i <min(ndata-j*nth, nth); ++i)
      {
        fkstart = fk + (i+j*nth)*ms*mt;
        fwstart = fw + i*nf1*nf2;
        deconvolveshuffle2d(2,1.0,fwkerhalf1,fwkerhalf2,ms,mt,(FLT*)fkstart,nf1,nf2,
                            fwstart,opts.modeord);
      }
    time_deconv+=timer.elapsedsec();
    // if (opts.debug) printf("amplify & copy in:\t %.3g s\n",timer.elapsedsec());

    // Step 2:  Call FFT
    timer.restart();
    FFTW_EX(p);
    time_fft+=timer.elapsedsec();
    // if (opts.debug) printf("fft (%d threads):\t %.3g s\n",nth,timer.elapsedsec());

    // Step 3: unspread (interpolate) from regular to irregular target pts
    timer.restart();
    #pragma omp parallel for private(ier_spread,cstart,fwstart)
    for (int i = 0; i<min(ndata-j*nth,nth); ++i)
    {
      cstart  = c + (i+j*nth)*nj;
      fwstart = fw + i*nf1*nf2;
      ier_spread = cnufftspreadwithsortidx(sort_indices,nf1,nf2,1,(FLT*)fwstart,nj,
                                           xj,yj,dummy,(FLT*)cstart,spopts,did_sort);
      if (ier_spread>0) {
        ier_spreads[i] = ier_spread;
        abort[i] = 1;
      }
    }
    time_spread+=timer.elapsedsec();
    for (int i = 0; i < nth; ++i)
    {
      if (abort[i])
      {
        return ier_spreads[i];
      }
    }
    // if (opts.debug) printf("unspread (ier=%d):\t %.3g s\n",ier_spread,timer.elapsedsec());
  }
  if (opts.debug) printf("[manysimul] amplify & copy in:\t %.3g s\n", time_deconv);
  if (opts.debug) printf("[manysimul] fft (%d threads):\t\t %.3g s\n", nth, time_fft);
  if (opts.debug) printf("[manysimul] unspread (ier=%d):\t\t %.3g s\n", 0,time_spread);

  if (opts.debug) printf("[manysimul] total execute time (exclude fftw_plan, etc.) %.3g s\n",
                         time_spread+time_fft+time_deconv);

  FFTW_FR(fw); free(fwkerhalf1); free(fwkerhalf2); free(sort_indices);
  if (opts.debug) printf("freed\n");
  return 0;
}

int finufft2d2many(int ndata, BIGINT nj, FLT* xj, FLT *yj, CPX* c, int iflag,
                   FLT eps, BIGINT ms, BIGINT mt, CPX* fk, nufft_opts opts)
/*
  Type-2 2D complex nonuniform FFT for multiple data.

	     cj[j,d] =  SUM   fk[k1,k2,d] exp(+/-i (k1 xj[j] + k2 yj[j]))
	               k1,k2
	     for j = 0,...,nj-1, d = 0,...,ndata-1
	     where sum is over -ms/2 <= k1 <= (ms-1)/2, -mt/2 <= k2 <= (mt-1)/2,

  Inputs:
    ndata  number of data
    nj     number of sources
    xj,yj  x,y locations of sources (each a size-nj FLT array) in [-3pi,3pi]
    fk     FLT complex array of Fourier transform values (size ms*mt*ndata,
           increasing fast in ms then slow in mt then in ndata, ie Fortran
           ordering). Along each dimension the ordering is set by opts.modeord.
    iflag  if >=0, uses + sign in exponential, otherwise - sign (int)
    eps    precision requested (>1e-16)
    ms,mt  numbers of Fourier modes given in x and y (int64)
           each may be even or odd;
           in either case the mode range is integers lying in [-m/2, (m-1)/2].
    opts   struct controlling options (see finufft.h)
  Outputs:
    cj     size-nj*ndata complex FLT array of target values, (ie, stored as
           2*nj*ndata FLTs interleaving Re, Im), increasing fast in nj then
           slow in ndata.
    returned value - 0 if success, else see ../docs/usage.rst

  The type 2 algorithm proceeds in three main steps (see [GL]).
  1) deconvolve (amplify) each Fourier mode, dividing by kernel Fourier coeff
  2) compute inverse FFT on uniform fine grid
  3) spread (dir=2, ie interpolate) data to regular mesh
  The kernel coeffs are precomputed in what is called step 0 in the code.
*/
{
  if (ndata<1) {        // factor is since fortran wants 1e-16 to be ok
    fprintf(stderr,"ndata should be at least 1 (ndata=%d)\n",ndata);
    return ERR_NDATA_NOTVALID;
  }

  if (opts.many_seq){
    return finufft2d2manyseq(ndata,nj,xj,yj,c,iflag,eps,ms,mt,fk,opts);
  } else {
    return finufft2d2manysimul(ndata,nj,xj,yj,c,iflag,eps,ms,mt,fk,opts);
  }
}
