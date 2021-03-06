

#include "ceigs.h"

#define F77_NAME(x) x ## _
#include <cs.h>

#include <math.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>



/**
 * @brief ARPACK header for dsaupd_.
 */
extern void 
F77_NAME(dsaupd)(int *ido, char *bmat, int *n, char *which,
      int *nev, double *tol, double *resid, int *ncv,
      double *v, int *ldv, int *iparam, int *ipntr,
      double *workd, double *workl, int *lworkl,
      int *info);
/**
 * @brief ARPACK header for dseupd_.
 */
extern void
F77_NAME(dseupd)(int *rvec, char *All, int *select, double *d,
      double *v, int *ldv, double *sigma, 
      char *bmat, int *n, char *which, int *nev,
      double *tol, double *resid, int *ncv, double *v2,
      int *ldv2, int *iparam, int *ipntr, double *workd,
      double *workl, int *lworkl, int *ierr);
/**
 * @brief LAPACK header for dgttrf_
 */
extern void
F77_NAME(dgttrf)(const int* n, double* dl, double* d,
       double* du, double* du2, int* ipiv, int* info);
/**
 * @brief LAPACK header for dgttrs_
 */
extern void
F77_NAME(dgttrs)(const char* trans, const int* n, const int* nrhs,
      double* dl, double* d, double* du, double* du2,
      int* ipiv, double* b, const int* ldb, int* info);


void eigs_optsDefault( EigsOpts_t *opts )
{
   memset( opts, 0, sizeof(EigsOpts_t) );

   opts->iters = 300;
   opts->tol   = 0.0; /* Maximum computer precision. */
   opts->sigma = 0.0; /* Sigma shift value. */
   opts->ncv   = 0;   /* Autosets based on nev. */
}


int eigs( int n, int nev, double *lambda, double *vec, const void *data_A, const void *data_M,
      EigsOrder_t order, EigsMode_t mode, const EigsDriverGroup_t *drvlist, const EigsOpts_t *opts )
{
   int i, j, k;
   int ido, ncv, ldv, lworkl, info, ierr, rvec, ret;
   double *resid, *v, *workd, *workl, *d;
   double tol, sigma;
   char *which, *bmat;
   int iparam[11], ipntr[11];
   int *sel;
   const EigsOpts_t *opts_use;
   EigsOpts_t opts_default;
   const EigsDriver_t *drv;
   const char *err;
   void *drv_data = NULL;

   /* Choose options to use. */
   if (opts == NULL) {
      eigs_optsDefault( &opts_default );
      opts_use = &opts_default;
   }
   else
      opts_use = opts;

   /* Use default drivers if not found. */
   if (drvlist == NULL)
#ifdef USE_UMFPACK
      drvlist = &eigs_drv_umfpack;
#else /* USE_UMFPACK */
      drvlist = &eigs_drv_cholesky;
#endif /* USE_UMFPACK */

   /* Set number of Lanczos vectors to use. */
   ncv   = opts_use->ncv;
   if (ncv <= 0) {
      ncv   = 2*nev; /* The largest number of basis vectors that will
                        be used in the Implicitly Restarted Arnoldi
                        Process.  Work per major iteration is
                        proportional to N*NCV*NCV. */
      if (ncv < 20)
         ncv = 20;
      if (ncv > n)
         ncv = n;
   }
   /* Sanity checks. */
   if (nev > n) {
      fprintf( stderr, "Condition \"nev <= n\" is not met.\n" );
      return -1;
   }
   if (nev+1 > ncv) {
      fprintf( stderr, "Condition \"nev+1 <= ncv\" is not met.\n" );
      return -2;
   }

   ido   = 0; /* Initialization of the reverse communication
                 parameter. */
   tol   = opts_use->tol; /* Sets the tolerance; tol<=0 specifies 
                             machine precision */
   resid = malloc( n * sizeof(double) );
   assert( resid != NULL );

   ldv = n;
   v   = malloc( ldv*ncv * sizeof(double) );
   assert( v != NULL );

   iparam[0] = 1;   /* Specifies the shift strategy (1->exact). */
   iparam[1] = 0;   /* Not referenced. */
   iparam[2] = opts_use->iters; /* Maximum number of iterations. */
   iparam[3] = 1;   /* NB blocksize in recurrence. ARPACK mentions only 1 is supported currently. */
   iparam[4] = 0;   /* nconv, number of Ritz values that satisfy covergence. */
   iparam[5] = 0;   /* Not referenced. */
   /* iparam[6] = 0; */ /* Set by driver. */
   iparam[7] = 0;   /* Output. */
   iparam[8] = 0;   /* Output. */
   iparam[9] = 0;   /* Output. */
   iparam[10] = 0;  /* Output. */
   sigma     = opts_use->sigma;

   /* Get Eigenvalue output order. */
   switch (order) {
      case EIGS_ORDER_LA: which = "LA"; break;
      case EIGS_ORDER_SA: which = "SA"; break;
      case EIGS_ORDER_LM: which = "LM"; break;
      case EIGS_ORDER_SM: which = "SM"; break;
      case EIGS_ORDER_BE: which = "BE"; break;
      default:
         fprintf( stderr, "Invalid eigenvalue order type.\n" );
         return -1;
   }

   /* Get operating mode. */
   switch (mode) {
      case EIGS_MODE_I_REGULAR:
         bmat      = "I";
         iparam[6] = 1;
         drv       = &drvlist->driver1;
         break;
      case EIGS_MODE_I_SHIFTINVERT:
         bmat      = "I";
         iparam[6] = 3;
         drv       = &drvlist->driver2;
         break;
      case EIGS_MODE_G_REGINVERSE:
         bmat      = "G";
         iparam[6] = 2;
         drv       = &drvlist->driver3;
         break;
      case EIGS_MODE_G_SHIFTINVERT:
         bmat      = "G";
         iparam[6] = 3;
         drv       = &drvlist->driver4;
         break;
      case EIGS_MODE_G_BUCKLING:
         bmat      = "G";
         iparam[6] = 4;
         drv       = &drvlist->driver5;
         break;
      case EIGS_MODE_G_CAYLEY:
         bmat      = "G";
         iparam[6] = 5;
         drv       = &drvlist->driver6;
         break;
      default:
         fprintf( stderr, "Invalid Eigs mode.\n" );
         return -1;
   }
   lworkl = ncv*(ncv+8); /* Length of the workl array */
   workd  = malloc( 3*n *    sizeof(double) );
   workl  = malloc( lworkl * sizeof(double) );
   assert( workd != NULL );
   assert( workl != NULL );

   info   = 0; /* Passes convergence information out of the iteration
                  routine. If set to 1 uses resid as an initial estimate
                  otherwise it gets initialized. */

   sel = malloc( ncv   * sizeof(int) );
   d   = malloc( 2*ncv * sizeof(double) ); /* This vector will return the eigenvalues from
                             the second routine, dseupd. */
   assert( sel != NULL );
   assert( d != NULL );

   rvec = (vec != NULL); /* Specifies that eigenvectors should not be calculated */
   ret  = 0; /* Default return value. */

   /* Make sure we have a driver. */
   if (drv->dsdrv == NULL) {
      fprintf( stderr, "Unable to find driver to use for ARPACK.\n" );
      ret = -1;
      goto err;
   }

   /* Initialize driver. */
   if (drv->init != NULL) {
      drv_data = drv->init( n, data_A, data_M, opts_use );
      if (drv_data == NULL) {
         fprintf( stderr, "Driver failed to initialize.\n" );
         ret = -1;
         goto err;
      }
   }

   /* Main loop using ARPACK. */
   do {
      /* Reverse Communication Interface of ARPACK. */
      F77_NAME(dsaupd)( &ido, bmat, &n, which, &nev, &tol, resid, 
               &ncv, v, &ldv, iparam, ipntr, workd, workl,
               &lworkl, &info );

      /* We'll use the driver that got set up. */
      drv->dsdrv( ido, n, workd, ipntr, data_A, data_M, drv_data );

   } while (ido != 99); /* Finish condition. */

   /* An error occurred. */
   if (info < 0) {
      switch (info) {
         case -1:
            err = "N must be positive.";
            break;
         case -2:
            err = "NEV must be positive.";
            break;
         case -3:
            err = "NCV must be greater than NEV and less than or equal to N.";
            break;
         case -4:
            err = "The maximum number of Arnoldi update iterations allowed must be greater than zero.";
            break;
         case -5:
            err = "WHICH must be one of 'LM', 'SM', 'LA', 'SA' or 'BE'.";
            break;
         case -6:
            err = "BMAT must be one of 'I' or 'G'.";
            break;
         case -7:
            err = "Length of private work array WORKL is not sufficient.";
            break;
         case -8:
            err = "Error return from trid. eigenvalue calculation; Informational error from LAPACK routine dsteqr.";
            break;
         case -9:
            err = "Starting vector is zero.";
            break;
         case -10:
            err = "IPARAM(7) must be 1,2,3,4,5.";
            break;
         case -11:
            err = "IPARAM(7) = 1 and BMAT = 'G' are incompatable.";
            break;
         case -12:
            err = "IPARAM(1) must be equal to 0 or 1.";
            break;
         case -13:
            err = "NEV and WHICH = 'BE' are incompatable.";
            break;
         case -9999:
            err = "Could not build an Arnoldi factorization. IPARAM(5) returns the size of the current Arnoldi factorization. The user is advised to check that";
            break;
         default:
            err = "Unknown error. Check the documentation of dsaupd.";
            break;
      }
      fprintf( stderr, "Error with dsaupd, info = %d\n%s\n\n", info, err );
      ret = -1;
      goto err;
   }

   /* From those results, the eigenvalues and vectors are
      extracted. */
   F77_NAME(dseupd)( &rvec, "All", sel, d, v, &ldv, &sigma, bmat,
            &n, which, &nev, &tol, resid, &ncv, v, &ldv,
            iparam, ipntr, workd, workl, &lworkl, &ierr );

   if (ierr!=0) {
      ret = -1;
      fprintf( stderr, "Error with dseupd, info = %d\nCheck the documentation of dseupd.\n\n", info );
      goto err;
   }
   else if (info==1)
      fprintf( stderr, "Maximum number of iterations reached.\n\n" );
   else if (info==3)
      fprintf( stderr, "No shifts could be applied during implicit Arnoldi update, try increasing NCV.\n\n" );

   /* Eigenvalues. */
   for (i=0; i<nev; i++) {
      k = nev-i-1;
      lambda[i] = d[k];
   }

   /* Eigenvectors. */
   if (rvec) {
      for (i=0; i<nev; i++) {
         k = nev-i-1;
         for (j=0; j<n; j++)
            vec[ i*n+j ] = v[ k*n + j ];
      }
   }

err:
   /* Clean up after the driver. */
   if (drv->free != NULL)
      drv->free( drv_data, opts_use );
   free( resid );
   free( v );
   free( workd );
   free( workl );
   free( sel );
   free( d );
   return ret;
}


void eigs_version( int *major, int *minor )
{
   *major = EIGS_VERSION_MAJOR;
   *minor = EIGS_VERSION_MINOR;
}


