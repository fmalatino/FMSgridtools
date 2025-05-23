/***********************************************************************
 *                   GNU Lesser General Public License
 *
 * This file is part of the GFDL FRE NetCDF tools package (FRE-NCTools).
 *
 * FRE-NCtools is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * FRE-NCtools is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FRE-NCTools.  If not, see
 * <http://www.gnu.org/licenses/>.
 **********************************************************************/

/*******************************************************************************
                             create_gnomonic_cubic_grid.c
  This file creates the 6 tiles for a gnomonic projection of a cubed sphere.
    It also creates nest grids if they are defined.
  Modifications:
  05/10/2020  -- Added multiple nest capability.  Bill Ramstrom, AOML/HRD
                 Nests can be specified on any parent tile, and can each be
                 different sizes. Nests with different refinement ratios
                 have NOT been tested and should be considered unsupported.
  12/07/2020  -- Global refinement bug fix. Kyle Ahern, AOML/HRD
  12/10/2020  -- Make multiple nest functionality consistent with latest
                 NOAA-GFDL source. Kyle Ahern, AOML/HRD
  03/05/2020  -- Enable many level Telescoping nests
                 (Nests within nests). Joseph Mouallem FV3/GFDL
  04/12/2021  -- Fixed several IMAs (Invalid Memory Access), memory leaks, and some
                 non-critical compiler warnings. Some notes related to MAs are scattered below.
                 Inorder to help reproduce the pre multinest GR (Global Refinement) awnsers,
                 the pre mulit-nest version of function create_gnomonic_cubic_grid was added back
                 as a second version of the function by that name.
                 Miguel Zuniga.
*******************************************************************************/

/**
 * \author Zhi Liang
*/


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "mpp.h"
#include "constant.h"
#include "mosaic_util.h"
#include "tool_util.h"
#include "create_hgrid.h"
#define  EPSLN10 (1.e-10)
#define  EPSLN4 (1.e-4)
#define  EPSLN5 (1.e-5)
#define  EPSLN7 (1.e-7)
#define  EPSLN8  (1.0e-8)

/* private subroutines */
void gnomonic_ed  (int ni, double* lamda, double* theta);
void gnomonic_angl(int ni, double* lamda, double* theta);
void gnomonic_dist(int ni, double* lamda, double* theta);
void cartesian_to_spherical(double x, double y, double z, double *lon, double *lat, double *r);
void spherical_to_cartesian(double lon, double lat, double r, double *x, double *y, double *z);
void symm_ed(int ni, double *lamda, double *theta);
void mirror_grid(int ni, int ntiles, double *x, double *y );
void mirror_latlon(double lon1, double lat1, double lon2, double lat2, double lon0,
         double lat0, double *lon, double *lat);
void rot_3d(int axis, double x1in, double y1in, double z1in, double angle, double *x2out,
       double *y2out, double *z2out, int degrees, int convert);
double excess_of_quad2(const double *vec1, const double *vec2, const double *vec3, const double *vec4 );
double angle_between_vectors2(const double *vec1, const double *vec2);
void plane_normal2(const double *P1, const double *P2, double *plane);
void calc_rotation_angle2(int nxp, double *x, double *y, double *angle_dx, double *angle_dy);
void cell_center(int ni, int nj, const double *lonc, const double *latc, double *lont, double *latt);
void cell_east(int ni, int nj, const double *lonc, const double *latc, double *lone, double *late);
void cell_north(int ni, int nj, const double *lonc, const double *latc, double *lonn, double *latn);
void calc_cell_area(int nx, int ny, const double *x, const double *y, double *area);
void direct_transform(double stretch_factor, int i1, int i2, int j1, int j2, double lon_p, double lat_p,
		      int n, double *lon, double *lat);
void suggest_target_lats(double stretch_factor, int i1, int i2, int j1, int j2, double lon_p, double lat_p, int ntiles,
                         double *lon, double *lat);
void cube_transform(double stretch_factor, int i1, int i2, int j1, int j2, double lon_p, double lat_p,
                    int n, double *lon, double *lat);
void setup_aligned_nest(int parent_ni, int parent_nj, const double *parent_xc, const double *parent_yc,
                        int halo, int refine_ratio, int istart, int iend, int jstart, int jend,
                        double *xc, double *yc, int is_gr);

void spherical_linear_interpolation(double beta, const double *p1, const double *p2, double *pb);

/*******************************************************************************
  void create_gnomonic_cubic_grid( int *npoints, int *nratio, char *method, char *orientation, double *x,
                          double *y, double *dx, double *dy, double *area, double *angle_dx,
                          double *angle_dy )
  create nomomic cubic grid. All six tiles grid will be generated.
*******************************************************************************/
void create_gnomonic_cubic_grid( char* grid_type, int *nlon, int *nlat, double *x, double *y,
                                 double *dx, double *dy, double *area, double *angle_dx,
                                 double *angle_dy, double shift_fac, int do_schmidt, int do_cube_transform, double stretch_factor,
                                 double target_lon, double target_lat, int num_nest_grids,
                                 int parent_tile[MAX_NESTS], int refine_ratio[MAX_NESTS], int istart_nest[MAX_NESTS],
                                 int iend_nest[MAX_NESTS], int jstart_nest[MAX_NESTS], int jend_nest[MAX_NESTS],
                                 int halo, int output_length_angle)
{
  const int ntiles = 6;
  int verbose = 1;
  long ntiles2, global_nest=0;

  long nx, ny, nxp, nyp, ni, nj, nip, njp;

  int nx_nest[MAX_NESTS], ny_nest[MAX_NESTS];
  int ni_nest[MAX_NESTS], nj_nest[MAX_NESTS];
  int ni_parent[MAX_NESTS], nj_parent[MAX_NESTS];
  int istart[MAX_NESTS], iend[MAX_NESTS], jstart[MAX_NESTS], jend[MAX_NESTS];

  int *nx_nest_arr=NULL;
  int *ny_nest_arr=NULL;
  int *ni_nest_arr=NULL;
  int *nj_nest_arr=NULL;

  long ni2, nj2, ni2p, nj2p, n1, n2;
  int *nxl=NULL, *nyl=NULL, *nil=NULL, *njl=NULL;
  int *tile_offset=NULL;
  int *tile_offset_supergrid=NULL;
  int *tile_offset_supergrid_m=NULL;
  int *tile_offset_area=NULL;

  long i, j, n, npts, nn;
  long npts_supergrid, npts_supergrid_m, npts_area;

  double p1[2], p2[2];
  double *lon=NULL, *lat=NULL;
  double *xc=NULL, *yc=NULL, *xtmp=NULL, *ytmp=NULL;
  double *xc2=NULL, *yc2=NULL;
  int    stretched_grid=0;

  /*
   *  make sure the first 6 tiles have the same grid size and
   *  the size in x and y-direction are the same
   */

  /* ntiles is a constant always equal to 6.  ntiles2 is variable, and includes the 6 global tiles plus any nests */

  if (verbose) fprintf(stderr, "[INFO] Starting create_gnomonic_grid with num_nest_grids=%d\n", num_nest_grids);

  for(n=0; n<ntiles; n++) {
    if(nlon[n] != nlat[n] ) mpp_error("create_gnomonic_cubic_grid: the grid size in x and y-direction "
                                      "should be the same for the 6 tiles of cubic sphere grid");
    if( nlon[n]%2 ) mpp_error("create_gnomonic_cubic_grid: supergrid size in x-direction should be divided by 2");
    if( nlat[n]%2 ) mpp_error("create_gnomonic_cubic_grid: supergrid size in y-direction should be divided by 2");
  }
  for(n=1; n<ntiles; n++) {
    if(nlon[n] != nlon[0]) mpp_error("create_gnomonic_cubic_grid: all six tiles should have same size");
  }

  // nx, ny variables correspond to the supergrid, which has twice as many points
  // ni, nj variables correspond to the number of cell centers
  // nip, njp variables correspond to the number of cell edges (i.e. ni+1, nj+1)

  nx  = nlon[0];
  ny  = nx;
  nxp = nx+1;
  nyp = ny+1;
  ni  = nx/2;
  nj  = ni;
  nip = ni+1;
  njp = nip;

  for (nn=0; nn < MAX_NESTS; nn++) {
    ni_nest[nn] = 0;
    nj_nest[nn] = 0;
    ni_parent[nn] = 0;
    nj_parent[nn] = 0;
  }

  ntiles2=ntiles;
  global_nest=0;

  if(num_nest_grids && parent_tile[0]== 0){
    global_nest = 1;
  }
  else {
    for (nn=0; nn < num_nest_grids; nn++) {
      ntiles2 = ntiles+num_nest_grids;
      if( (istart_nest[nn]+1)%2 ) mpp_error("create_gnomonic_cubic_grid: istart_nest+1 is not divisible by 2");
      if( iend_nest[nn]%2 ) mpp_error("create_gnomonic_cubic_grid: iend_nest is not divisible by 2");
      if( (jstart_nest[nn]+1)%2 ) mpp_error("create_gnomonic_cubic_grid: jstart_nest+1 is not divisible by 2");
      if( jend_nest[nn]%2 ) mpp_error("create_gnomonic_cubic_grid: jend_nest is not divisible by 2");
      istart[nn] = (istart_nest[nn]+1)/2;
      iend[nn]   = iend_nest[nn]/2;
      jstart[nn] = (jstart_nest[nn]+1)/2;
      jend[nn]   = jend_nest[nn]/2;
      ni_nest[nn] = (iend[nn]-istart[nn]+1)*refine_ratio[nn];
      nj_nest[nn] = (jend[nn]-jstart[nn]+1)*refine_ratio[nn];

      nx_nest[nn] = ni_nest[nn]*2;
      ny_nest[nn] = nj_nest[nn]*2;

      /* Setup parent ni */
      if (parent_tile[nn] <= ntiles) {
        ni_parent[nn] = ni;
        nj_parent[nn] = nj;
      }
      else {
        ni_parent[nn] = ni_nest[parent_tile[nn]-ntiles-1];
        nj_parent[nn] = nj_nest[parent_tile[nn]-ntiles-1];
      }
    }
  }

  if (verbose) {
    fprintf(stderr, "[INFO] ntiles: %d num_nest_grids: %d ntiles2: %ld\n", ntiles, num_nest_grids, ntiles2);
  }

  /*  Since many of the variables for the global and nest tiles are stored in 1D arrays,
      we generate a set of indices that navigate to the start of each global and nest tile.
      ntiles is set to 1 for regional or 6 for global
      ntiles2 is ntiles + the number of nests

      nxl, nyl indicate supergrid size for each tile
      nil, njl indicate model grid size for each tile
  */

  nxl = (int *)malloc(ntiles2*sizeof(int));
  nyl = (int *)malloc(ntiles2*sizeof(int));
  nil = (int *)malloc(ntiles2*sizeof(int));
  njl = (int *)malloc(ntiles2*sizeof(int));

  nx_nest_arr = (int *)malloc(num_nest_grids*sizeof(int));
  ny_nest_arr = (int *)malloc(num_nest_grids*sizeof(int));
  ni_nest_arr = (int *)malloc(num_nest_grids*sizeof(int));
  nj_nest_arr = (int *)malloc(num_nest_grids*sizeof(int));

  for (nn=0; nn < num_nest_grids; nn++) {
    nx_nest_arr[nn] = nx_nest[nn];
    ny_nest_arr[nn] = ny_nest[nn];
    ni_nest_arr[nn] = ni_nest[nn];
    nj_nest_arr[nn] = nj_nest[nn];
  }

  //Note : tile_offet arrays below are to small (by 1) for GR (Global Refinement) runs and
  // result in several IMAs (Invalid Memory Access)
  tile_offset = (int *)malloc(ntiles2*sizeof(int));
  tile_offset_supergrid = (int *)malloc(ntiles2*sizeof(int));
  tile_offset_supergrid_m = (int *)malloc(ntiles2*sizeof(int));
  tile_offset_area = (int *)malloc(ntiles2*sizeof(int));

  for(n=0; n<ntiles; n++) {
    nxl[n] = nx;
    nyl[n] = ny;
    nil[n] = ni;
    njl[n] = nj;
  }

  /* Use of these arrays permits different sized nests */
  if (global_nest != 1) {//This check removes one IMA with GR.
  for (nn = 0; nn < num_nest_grids; nn++) {
    nxl[nn + ntiles] = nx_nest_arr[nn];
    nyl[nn + ntiles] = ny_nest_arr[nn];
    nil[nn + ntiles] = ni_nest_arr[nn];
    njl[nn + ntiles] = nj_nest_arr[nn];
  }
  }

  if (verbose) {
    fprintf(stderr, "[INFO] INDEX ntiles: %d ntiles2: %ld\n", ntiles, ntiles2);
    for(n=0; n<ntiles2; n++) {
      fprintf(stderr, "[INFO] INDEX n: %ld nxl[n]: %d nyl[n]: %d nil[n]: %d njl[n]: %d\n",
              n, nxl[n], nyl[n], nil[n], njl[n]);
    }
  }
  /* for global nest grid, set ni to the coarse grid size */
  /* TODO -- can this code handle multiple different refinement ratios for global nests? */
  if(global_nest) {
    ni /= refine_ratio[0];
    nj /= refine_ratio[0];
  }
  nip=ni+1;
  njp=nj+1;

  if ( (do_schmidt || do_cube_transform) && fabs(stretch_factor-1.) > EPSLN5 ) stretched_grid = 1;

  lon = (double *)malloc(nip*nip*sizeof(double));
  lat = (double *)malloc(nip*nip*sizeof(double));

  if(strcmp(grid_type, "gnomonic_ed")==0 )
    gnomonic_ed(  ni, lon, lat);
  else if(strcmp(grid_type,"gnomonic_dist")==0)
    gnomonic_dist(ni, lon, lat);
  else if(strcmp(grid_type,"gnomonic_angl")==0)
    gnomonic_angl(ni, lon, lat);
  else mpp_error("create_gnomonic_cubic_grid: grid type should be 'gnomonic_ed', "
                 "'gnomonic_dist' or 'gnomonic_angl'");

  symm_ed(ni, lon, lat);

  // Cycle through all of the tiles; global and nests, adding enough points based on the dimensions
  // The 6 cubed-sphere tiles are square thus, nil=njl, but the nests can be rectangular
  npts = 0;
  npts_supergrid = 0;
  npts_supergrid_m = 0;
  npts_area = 0;

  for (n=0; n<ntiles2; n++) {
    tile_offset[n] = npts;
    tile_offset_supergrid[n] = npts_supergrid;
    tile_offset_supergrid_m[n] = npts_supergrid_m;
    tile_offset_area[n] = npts_area;

    if (verbose) {
      fprintf(stderr, "[INFO] INDEX OFFSET n: %ld tile_offset[n]: %d tile_offset_supergrid[n]: %d \
              tile_offset_supergrid_m[n]: %d tile_offset_area[n]: %d \n",
			        n, tile_offset[n],  tile_offset_supergrid[n],  tile_offset_supergrid_m[n],  tile_offset_area[n]);
      }

    npts += (nil[n] + 1) * (njl[n] + 1);
    npts_supergrid += (nxl[n] + 1) * (nyl[n] + 1);
    npts_supergrid_m += nxl[n] * (nyl[n] + 1);    // needed for grids of dx, dy
    npts_area += nxl[n] * nyl[n];    // needed for area
  }

  if (verbose) fprintf(stderr, "[INFO] INDEX OFFSET npts: %ld\n", npts);

  xc = (double *)malloc(npts*sizeof(double));
  yc = (double *)malloc(npts*sizeof(double));

  for(j=0; j<nip; j++) {
    for(i=0; i<nip; i++) {
      xc[j*nip+i] = lon[j*nip+i] - M_PI;
      yc[j*nip+i] = lat[j*nip+i];
    }
  }

  /* mirror_grid assumes that the tile=1 is centered on equator
     and greenwich meridian Lon[-pi,pi]  */
  mirror_grid(ni, ntiles, xc, yc);

  // Operate only on the 6 parent tiles
  for(n=0; n<ntiles*nip*nip; n++) {
    /* This will result in the corner close to east coast of china */
    if( do_schmidt == 0 && do_cube_transform == 0 && shift_fac > EPSLN4) xc[n] -= M_PI/18.;
    if(xc[n] < 0.) xc[n] += 2.*M_PI;
    if(fabs(xc[n]) < EPSLN10) xc[n] = 0;
    if(fabs(yc[n]) < EPSLN10) yc[n] = 0;
  }

  /* ensure consistency on the boundary between tiles */
  for(j=0; j<nip; j++) {
    xc[  nip*nip+j*nip] = xc[j*nip+ni];                 /* 1E -> 2W */
    yc[  nip*nip+j*nip] = yc[j*nip+ni];                 /* 1E -> 2W */
    xc[2*nip*nip+j*nip] = xc[ni*nip+ni-j];              /* 1N -> 3W */
    yc[2*nip*nip+j*nip] = yc[ni*nip+ni-j];              /* 1N -> 3W */
  }
  for(i=0; i<nip; i++) {
    xc[4*nip*nip+ni*nip+i] = xc[(ni-i)*nip];            /* 1W -> 5N */
    yc[4*nip*nip+ni*nip+i] = yc[(ni-i)*nip];            /* 1W -> 2N */
    xc[5*nip*nip+ni*nip+i] = xc[i];                     /* 1S -> 6N */
    yc[5*nip*nip+ni*nip+i] = yc[i];                     /* 1S -> 6N */
    xc[2*nip*nip+i]        = xc[nip*nip+ni*nip+i];      /* 2N -> 3S */
    yc[2*nip*nip+i]        = yc[nip*nip+ni*nip+i];      /* 2N -> 3S */
    xc[3*nip*nip+i]        = xc[nip*nip+(ni-i)*nip+ni];  /* 2E -> 4S */
    yc[3*nip*nip+i]        = yc[nip*nip+(ni-i)*nip+ni];  /* 2E -> 4S */
  }
  for(j=0; j<nip; j++) {
    xc[5*nip*nip+j*nip+ni] = xc[nip*nip+ni-j];          /* 2S -> 6E */
    yc[5*nip*nip+j*nip+ni] = yc[nip*nip+ni-j];          /* 2S -> 6E */
    xc[3*nip*nip+j*nip]    = xc[2*nip*nip+j*nip+ni];    /* 3E -> 4W */
    yc[3*nip*nip+j*nip]    = yc[2*nip*nip+j*nip+ni];    /* 3E -> 4W */
    xc[4*nip*nip+j*nip]    = xc[2*nip*nip+ni*nip+ni-j]; /* 3N -> 5W */
    yc[4*nip*nip+j*nip]    = yc[2*nip*nip+ni*nip+ni-j]; /* 3N -> 5W */
  }
  for(i=0; i<nip; i++) {
    xc[4*nip*nip+i] = xc[3*nip*nip+ni*nip+i];           /* 4N -> 5S */
    yc[4*nip*nip+i] = yc[3*nip*nip+ni*nip+i];           /* 4N -> 5S */
    xc[5*nip*nip+i] = xc[3*nip*nip+(ni-i)*nip+ni];      /* 4E -> 6S */
    yc[5*nip*nip+i] = yc[3*nip*nip+(ni-i)*nip+ni];      /* 4E -> 6S */
  }
  for(j=0; j<nip; j++) {
    xc[5*nip*nip+j*nip] = xc[4*nip*nip+j*nip+ni];    /* 5E -> 6W */
    yc[5*nip*nip+j*nip] = yc[4*nip*nip+j*nip+ni];    /* 5E -> 6W */
  }

  /* Schmidt transformation */
  if ( do_schmidt ) {
    /*In general for a given stretch factor and target latitude the resulting stretch grid will not have the poles as grid points.
      This may cause issues later with other tools such as exchange grid generator manifested as tiling errors and cells with a wrong land mask.
      The following call searches for target latitudes close to the specified one that would allow both North and South poles
      to be grid points in the resulting stretched grid.
      Currently this just prints the advisory target latitude values and will not change the grid in any way. It is possible to add an option later to use the adjusted value.
    */
    if(num_nest_grids == 0) suggest_target_lats(stretch_factor, 0, ni, 0, ni, target_lon*D2R, target_lat*D2R, ntiles, xc, yc);

    for(n=0; n<ntiles; n++) {

      if (verbose) fprintf(stderr, "[INFO] Calling direct_transform for tile %ld\n", n);

      direct_transform(stretch_factor, 0, ni, 0, ni, target_lon*D2R, target_lat*D2R,
                       n, xc+n*nip*nip, yc+n*nip*nip);

    }
  } else if ( do_cube_transform ) {
    for (n=0; n<ntiles; n++) {

      if (verbose) fprintf(stderr, "[INFO] Calling cube_transform for tile %ld\n", n);

      cube_transform(stretch_factor, 0, ni, 0, ni, target_lon*D2R, target_lat*D2R,
                     n, xc+n*nip*nip, yc+n*nip*nip);
    }
  }

  /* get nest grid */
  if(global_nest) {
    npts = ntiles*nip*nip;
    xc2 = (double *)malloc(npts*sizeof(double));
    yc2 = (double *)malloc(npts*sizeof(double));
    for(n=0; n<npts; n++) {
      xc2[n] = xc[n];
      yc2[n] = yc[n];
    }
    free(xc);
    free(yc);
    ni2  = ni;
    ni2p = nip;
    ni   = nx/2;
    nip  = ni + 1;
    npts = ntiles*nip*nip;
    xc = (double *)malloc(npts*sizeof(double));
    yc = (double *)malloc(npts*sizeof(double));
    for(n=0; n<ntiles; n++) {
      fprintf(stderr,"[INFO] calling setup_aligned_nest, n=%ld\n",n);
      /* zeroth index of refine_ratio array                  *
       * is assigned to all tiles if global_nest = 1 [Ahern] */
      setup_aligned_nest(ni2, ni2, xc2+ni2p*ni2p*n, yc2+ni2p*ni2p*n, 0, refine_ratio[0],
                         1, ni2, 1, ni2, xc+n*nip*nip, yc+n*nip*nip, 1 );
    }
  }
  else if( num_nest_grids > 0 ) {
    for (nn=0; nn < num_nest_grids; nn++) {
      if (verbose) {
        fprintf(stderr,
                "[INFO] Processing setup_aligned_nest for nest %ld . ntiles=%d parent_tile[nn]: %d\n",
                nn, ntiles, parent_tile[nn]);
      }
      /* Setup aligned nest -- final two arguments are memory locations for data to be returned */
      /* The pointer arithmetic is complicated */
      /* ni = number of points on supergrid */
      /* nip = ni + 1 */
      setup_aligned_nest(ni_parent[nn], nj_parent[nn], xc+tile_offset[parent_tile[nn]-1],
                         yc+tile_offset[parent_tile[nn]-1], halo, refine_ratio[nn],
                         istart[nn], iend[nn], jstart[nn], jend[nn],
                         xc+tile_offset[ntiles+nn], yc+tile_offset[ntiles+nn], 0);
    }

    if (verbose) fprintf(stderr, "[INFO] Completed processing setup_aligned_nest for nest(s)\n");
  }

  /* calculate grid box center location */

  ni2 = 0;
  nj2 = 0;
  for(n=0; n<ntiles2; n++) {
    if(nil[n]>ni2) ni2 = nil[n];
    if(njl[n]>nj2) nj2 = njl[n];
  }
  ni2p = ni2+1;
  nj2p = nj2+1;
  xtmp = (double *)malloc(ni2p*nj2p*sizeof(double));
  ytmp = (double *)malloc(ni2p*nj2p*sizeof(double));

  /* Setting the x, y values for each tile */
  /* Not clear that data is handled correctly for nested tiles, though. */

  /* Iterate over all of the tiles                                                                   */
  /* Copy the lat/lons into the x, y array                                                           */
  /*     C-cell                                                                                      */
  /*     Center                                                                                      */
  /*     East                                                                                        */
  /*     North                                                                                       */

  for(n=0; n<ntiles2; n++) {
    // long n1,n2 // aren't these already declared at the function start? [Ahern]
    long min_n1 = -1;
    long max_n1 = -1;

    /* copy C-cell to supergrid */
    if (verbose) {
      fprintf(stderr, "[INFO] INDEX fill x and y from C-cell. n=%ld n*nxp*nxp=%ld tile_offset[n]: %d \
              tile_offset_supergrid[n]: %d njl[n]: %d nil[n]: %d\n",
              n, n*nxp*nxp, tile_offset[n], tile_offset_supergrid[n], njl[n], nil[n]);
      fprintf(stderr, "[INFO] START fill x and y from C-cell.  n=%ld tile_offset_supergrid[n]: %d \n",
              n, tile_offset_supergrid[n]);
    }

    for(j=0; j<=njl[n]; j++){
      for(i=0; i<=nil[n]; i++) {
        n1 = tile_offset_supergrid[n] + j*2*(2*nil[n]+1) + i*2;
        n2 = tile_offset[n] + j*(nil[n]+1)+i;

        x[n1]=xc[n2];
        y[n1]=yc[n2];

        if (verbose){
          if (n1 < min_n1 || min_n1 == -1) min_n1 = n1;
          if (n1 > max_n1) max_n1 = n1;
        }
      }
    }

    /* cell center and copy to super grid */
    cell_center(nil[n], njl[n], xc + tile_offset[n], yc + tile_offset[n], xtmp, ytmp);
    if (verbose) fprintf(stderr, "[INFO] CENTER n: %ld n*nip*nip: %ld tile_offset[n]: %d\n", n, n*nip*nip, tile_offset[n]);
    for(j=0; j<njl[n]; j++) {
      for(i=0; i<nil[n]; i++) {
        // Offset of 2 for i=0, j=0
        n1 = tile_offset_supergrid[n] + (j*2+1)*(2*nil[n]+1)+i*2+1;
        n2 = j*nil[n]+i;   // WDR why does this not have a tile_offset??
        x[n1]=xtmp[n2];
        y[n1]=ytmp[n2];

        if (verbose){
          if (n1 < min_n1) min_n1 = n1;
          if (n1 > max_n1) max_n1 = n1;
        }

      }
    }

    /* cell east and copy to super grid */
    cell_east(nil[n], njl[n], xc + tile_offset[n], yc + tile_offset[n], xtmp, ytmp);
    for(j=0; j<njl[n]; j++){
      for(i=0; i<=nil[n]; i++) {
        // Offset of 2*nil[n] + 1 for i=0, j=0
        n1 = tile_offset_supergrid[n] + (j*2+1)*(2*nil[n]+1)+i*2;
        n2 = j*(nil[n]+1)+i;   // WDR why does this not have a tile_offset??
        x[n1]=xtmp[n2];
        y[n1]=ytmp[n2];

        if (verbose){
          if (n1 < min_n1) min_n1 = n1;
          if (n1 > max_n1) max_n1 = n1;
        }
      }
    }

    /* cell north and copy to super grid */
    cell_north(nil[n], njl[n], xc + tile_offset[n], yc + tile_offset[n], xtmp, ytmp);
    for(j=0; j<=njl[n]; j++){
      for(i=0; i<nil[n]; i++) {
        // Offset of 1 for i=0, j=0
        n1 = tile_offset_supergrid[n] + (j*2)*(2*nil[n]+1)+i*2+1;
        n2 = j*nil[n]+i;   // WDR why does this not have a tile_offset??
        x[n1]=xtmp[n2];
        y[n1]=ytmp[n2];

        if (verbose){
          if (n1 < min_n1) min_n1 = n1;
          if (n1 > max_n1) max_n1 = n1;
        }
      }
    }

    if (verbose) fprintf(stderr,
                         "[INFO] INDEX tile: %ld min_n1: %ld max_n1: %ld max_n1 - min_n1: %ld sqrt(max_n1 - min_n1 + 1): %f\n",
                         n, min_n1, max_n1, max_n1 - min_n1, sqrt(max_n1 - min_n1 + 1));
  }

  free(xtmp);
  free(ytmp);

  /* calculate grid cell length */
  if (output_length_angle) {
    /* Calculate dx */
    for(n=0; n<ntiles2; n++) {
      if (verbose) fprintf(stderr, "[INFO] Calculating dx for tile n: %ld ntiles2: %ld\n", n, ntiles2);
      for(j=0; j<=nyl[n]; j++) {
        for(i=0; i<nxl[n]; i++) {

          p1[0] = x[tile_offset_supergrid[n] + j*(nxl[n]+1)+i];
          p1[1] = y[tile_offset_supergrid[n] + j*(nxl[n]+1)+i];
          p2[0] = x[tile_offset_supergrid[n] + j*(nxl[n]+1)+i+1];
          p2[1] = y[tile_offset_supergrid[n] + j*(nxl[n]+1)+i+1];

          dx[tile_offset_supergrid_m[n] + j*nxl[n]+i] = great_circle_distance(p1, p2);

        } /* i < nxl[n] */
      } /* j <= nyl[n] */
    } /* n < ntiles2 */

    /* Calculate dy */
    for(n=0; n<ntiles2; n++) {
      if (verbose) fprintf(stderr, "[INFO] Calculating dy for tile n: %ld ntiles: %d ntiles2: %ld\n", n, ntiles, ntiles2);

      if( stretched_grid || (n >= 6) ) {
        for(j=0; j<nyl[n]; j++) {
          for(i=0; i<=nxl[n]; i++) {
            p1[0] = x[tile_offset_supergrid[n] + j*(nxl[n]+1)+i];
            p1[1] = y[tile_offset_supergrid[n] + j*(nxl[n]+1)+i];
            p2[0] = x[tile_offset_supergrid[n] + (j+1)*(nxl[n]+1)+i];
            p2[1] = y[tile_offset_supergrid[n] + (j+1)*(nxl[n]+1)+i];

            dy[tile_offset_supergrid_m[n] + j*(nxl[n]+1)+i] = great_circle_distance(p1, p2);
          } /* i <= nxl[n] */
        } /* j < nyl[n] */
      } else /* (!(stretched_grid || n >= 6)) */ {
        for(j=0; j<nyp; j++) {
          for(i=0; i<nx; i++) dy[tile_offset_supergrid_m[n] + i*nxp+j] = dx[tile_offset_supergrid_m[n] + j*nx+i];
        }
      } /* else (!(stretched_grid || n >= 6)) */
    } /* n < ntiles2 */

    /* ensure consistency on the boundaries between tiles */
    for(j=0; j<nx; j++) {
      long n11, n21, n31, n41, n51, n61, n71, n81, n91;
      long n12, n22, n32, n42, n52, n62, n72, n82, n92;

      n11 = j*nxp;
      n12 = 4*nx*nxp+nx*nx+nx-j-1;

      n21 = j*nxp+nx;
      n22 = nxp*nx+j*nxp;

      n31 = nxp*nx+j*nxp+nx;
      n32 = 3*nx*nxp+(nx-j-1);

      n41 = 2*nxp*nx+j*nxp;
      n42 = nx*nx+nx-j-1;

      n51 = 2*nxp*nx+j*nxp+nx;
      n52 = 3*nxp*nx+j*nxp;

      n61 = 3*nxp*nx+j*nxp+nx;
      n62 = 5*nx*nxp+(nx-j-1);

      n71 = 4*nxp*nx+j*nxp;
      n72 = 2*nx*nxp+nx*nx+nx-j-1;

      n81 = 4*nxp*nx+j*nxp+nx;
      n82 = 5*nxp*nx+j*nxp;

      n91= 5*nxp*nx+j*nxp+nx;
      n92 = nx*nxp+(nx-j-1);

      dy[n11] = dx[n12]; /* 5N -> 1W */
      dy[n21] = dy[n22]; /* 2W -> 1E */
      dy[n31] = dx[n32]; /* 4S -> 2E */
      dy[n41] = dx[n42]; /* 1N -> 3W */
      dy[n51] = dy[n52]; /* 4W -> 3E */
      dy[n61] = dx[n62]; /* 4S -> 2E */
      dy[n71] = dx[n72]; /* 3N -> 5W */
      dy[n81] = dy[n82]; /* 6W -> 5E */
      dy[n91] = dx[n92]; /* 2S -> 6E */
    } /* j < nx */
  } /* output_length_angle */

  if(do_schmidt) { /* calculate area for each tile */
    for(n=0; n<ntiles; n++) {
      if (verbose) fprintf(stderr, "[INFO] call calc_cell_area do_schmidt for tile n=%ld\n", n);
      calc_cell_area(nx, ny, x + tile_offset_supergrid[n], y + tile_offset_supergrid[n], area + tile_offset_area[n]);
    }
  } else if (do_cube_transform) { /* calculate area for each tile */
    for(n=0; n<ntiles; n++) {
      if (verbose) fprintf(stderr, "[INFO] call calc_cell_area do_cube_transform for tile n=%ld\n", n);
      calc_cell_area(nx, ny, x + tile_offset_supergrid[n], y + tile_offset_supergrid[n], area + tile_offset_area[n]);
    }
  }  else {
    if (verbose) fprintf(stderr, "[INFO] call calc_cell_area for first tile.\n");
    calc_cell_area(nx, ny, x, y, area);
    for(j=0; j<nx; j++) {
      for(i=0; i<nx; i++) {
        double ar;
        /* all the faces have the same area */
        ar = area[j*nx+i];
        area[nx*nx+j*nx+i] = ar;
        area[2*nx*nx+j*nx+i] = ar;
        area[3*nx*nx+j*nx+i] = ar;
        area[4*nx*nx+j*nx+i] = ar;
        area[5*nx*nx+j*nx+i] = ar;
      }
    }
  }

  /* calculate nested grid area */
  for (nn=0; nn < num_nest_grids; nn++) {

    if (verbose) {
      fprintf(stderr, "[INFO] call calc_cell_area for nest nn=%ld tile n=%ld\n", nn, ntiles+nn);
    }
    calc_cell_area(nx_nest_arr[nn], ny_nest_arr[nn],
                   x + tile_offset_supergrid[ntiles+nn],
                   y + tile_offset_supergrid[ntiles+nn],
                   area + tile_offset_area[ntiles+nn]);
      }

  if (output_length_angle) {
    /*calculate rotation angle, just some workaround, will modify this in the future. */
    calc_rotation_angle2(nxp, x, y, angle_dx, angle_dy );

    //since angle is used in the model, set angle to 0 for nested region
    for(nn=0; nn < num_nest_grids; nn++) {
      //Note: Changed "<=" to "<" below to remove one IMA (twice) for GR AND non-GR runs.
      // For GR runs, it also changed four result numbers.
      for(i=0; i<(nx_nest_arr[nn]+1)*(ny_nest_arr[nn]+1); i++) {
        angle_dx[tile_offset_supergrid[ntiles+nn] + i]=0;
        angle_dy[tile_offset_supergrid[ntiles+nn] + i]=0;
      }
    }
  }

  /* convert grid location from radians to degree */
  if (verbose) fprintf(stderr, "[INFO] Convert radians to degrees: npts = %ld npts_supergrid: %ld\n",
                       npts, npts_supergrid);

  for(i=0; i<npts_supergrid; i++) {
    x[i] = x[i]*R2D;
    y[i] = y[i]*R2D;
  }


  free(xc);
  free(yc);
  free(nxl);
  free(nyl);
  free(nil);
  free(njl);
  free(nx_nest_arr);
  free(ny_nest_arr);
  free(ni_nest_arr);
  free(nj_nest_arr);
  free(tile_offset);
  free(tile_offset_supergrid);
  free(tile_offset_supergrid_m);
  free(tile_offset_area);
  free(lon);
  free(lat);
  free(xc2);
  free(yc2);
} /* void create_gnomonic_cubic_grid */

/*
  Function create_gnomonic_cubic_grid_GR is mostly (some lines deleted) the version of
  the function just prior to the multi nest version.

  This function should only be called for GR  computations.

  Unsuccessful attempts were made to make the current general purpose
  create_gnomonic_cubic_grid version reproduce the pre multi-nest version answers for
  GR computations. This current GR version does reproduce the answers within a small
  tolerance (e.g. to about the tenth decimal digit for fields x and y on gcc 9.3 compiler).

  TODO: Update general purpose multi-nest version of create_gnomonic_cubic_grid so that it
  reproduces the pre multi-nest answers; remove the GR version below.
*/

void create_gnomonic_cubic_grid_GR( char* grid_type, int *nlon, int *nlat, double *x, double *y,
				double *dx, double *dy, double *area, double *angle_dx,
				 double *angle_dy, double shift_fac, int do_schmidt, int do_cube_transform, double stretch_factor,
				 double target_lon, double target_lat, int nest_grid,
				 int parent_tile, int refine_ratio, int istart_nest,
				 int iend_nest, int jstart_nest, int jend_nest, int halo, int output_length_angle)
{
  const int ntiles = 6;
  long ntiles2, global_nest=0;
  long nx, ny, nxp, nyp, ni, nj, nip;
  long ni_nest, nj_nest, nx_nest, ny_nest;
  long istart, iend, jstart, jend;
  long ni2, nj2, ni2p, nj2p, n1, n2;
  int *nxl=NULL, *nyl=NULL, *nil=NULL, *njl=NULL;
  long i,j,n, npts;
  double p1[2], p2[2];
  double *lon=NULL, *lat=NULL;
  double *xc=NULL, *yc=NULL, *xtmp=NULL, *ytmp=NULL;
  double *xc2=NULL, *yc2=NULL;
  int    stretched_grid=0;

  /* make sure the first 6 tiles have the same grid size and
     the size in x and y-direction are the same
  */

  for(n=0; n<ntiles; n++) {
    if(nlon[n] != nlat[n] ) mpp_error("create_gnomonic_cubic_grid: the grid size in x and y-direction "
			  	  "should be the same for the 6 tiles of cubic sphere grid");
    if( nlon[n]%2 ) mpp_error("create_gnomonic_cubic_grid: supergrid size in x-direction should be divided by 2");
    if( nlat[n]%2 ) mpp_error("create_gnomonic_cubic_grid: supergrid size in y-direction should be divided by 2");
  }
  for(n=1; n<ntiles; n++) {
    if(nlon[n] != nlon[0]) mpp_error("create_gnomonic_cubic_grid: all six tiles should have same size");
  }

  nx  = nlon[0];
  ny  = nx;
  nxp = nx+1;
  nyp = ny+1;
  ni  = nx/2;
  nj  = ni;
  nip = ni+1; //Note: njp = nip
  ni_nest = 0;
  nj_nest = 0;
  ntiles2=ntiles;
  global_nest=0;
  if(nest_grid && parent_tile== 0)
    global_nest = 1;
  else{
    mpp_error("use only for global nest");
  }
  nx_nest = ni_nest*2;
  ny_nest = nj_nest*2;

  /* nxl/nyl supergrid size, nil, njl model grid size */
  nxl = (int *)malloc(ntiles2*sizeof(int));
  nyl = (int *)malloc(ntiles2*sizeof(int));
  nil = (int *)malloc(ntiles2*sizeof(int));
  njl = (int *)malloc(ntiles2*sizeof(int));

  for(n=0; n<ntiles; n++) {
    nxl[n] = nx;
    nyl[n] = ny;
    nil[n] = ni;
    njl[n] = nj;
  }
  if(ntiles2 > ntiles) {
    nxl[ntiles] = nx_nest;
    nyl[ntiles] = ny_nest;
    nil[ntiles] = ni_nest;
    njl[ntiles] = nj_nest;
  }

  /* for global nest grid, set ni to the coarse grid size */
  if(global_nest) {
    ni /= refine_ratio;
    nj /= refine_ratio;
  }
  nip=ni+1;

  if ( (do_schmidt || do_cube_transform) && fabs(stretch_factor-1.) > EPSLN5 ) stretched_grid = 1;

  lon = (double *)malloc(nip*nip*sizeof(double));
  lat = (double *)malloc(nip*nip*sizeof(double));

  if(strcmp(grid_type, "gnomonic_ed")==0 )
    gnomonic_ed(  ni, lon, lat);
  else if(strcmp(grid_type,"gnomonic_dist")==0)
    gnomonic_dist(ni, lon, lat);
  else if(strcmp(grid_type,"gnomonic_angl")==0)
    gnomonic_angl(ni, lon, lat);
  else mpp_error("create_gnomonic_cubic_grid: grid type should be 'gnomonic_ed', "
                 "'gnomonic_dist' or 'gnomonic_angl'");

  symm_ed(ni, lon, lat);

  npts = ntiles*nip*nip;
  if(ntiles2>ntiles) npts += (ni_nest+1)*(nj_nest+1);

  xc = (double *)malloc(npts*sizeof(double));
  yc = (double *)malloc(npts*sizeof(double));

  for(j=0; j<nip; j++) {
    for(i=0; i<nip; i++) {
      xc[j*nip+i] = lon[j*nip+i] - M_PI;
      yc[j*nip+i] = lat[j*nip+i];
    }
  }

  /* mirror_grid assumes that the tile=1 is centered on equator
     and greenwich meridian Lon[-pi,pi]  */
  mirror_grid(ni, ntiles, xc, yc);

  for(n=0; n<ntiles*nip*nip; n++) {
    /* This will result in the corner close to east coast of china */
    if( do_schmidt == 0 && do_cube_transform==0 && shift_fac > EPSLN4) xc[n] -= M_PI/18.;
    if(xc[n] < 0.) xc[n] += 2.*M_PI;
    if(fabs(xc[n]) < EPSLN10) xc[n] = 0;
    if(fabs(yc[n]) < EPSLN10) yc[n] = 0;
  }

  /* ensure consistency on the boundary between tiles */
  for(j=0; j<nip; j++) {
    xc[  nip*nip+j*nip] = xc[j*nip+ni];                 /* 1E -> 2W */
    yc[  nip*nip+j*nip] = yc[j*nip+ni];                 /* 1E -> 2W */
    xc[2*nip*nip+j*nip] = xc[ni*nip+ni-j];              /* 1N -> 3W */
    yc[2*nip*nip+j*nip] = yc[ni*nip+ni-j];              /* 1N -> 3W */
  }
  for(i=0; i<nip; i++) {
    xc[4*nip*nip+ni*nip+i] = xc[(ni-i)*nip];            /* 1W -> 5N */
    yc[4*nip*nip+ni*nip+i] = yc[(ni-i)*nip];            /* 1W -> 2N */
    xc[5*nip*nip+ni*nip+i] = xc[i];                     /* 1S -> 6N */
    yc[5*nip*nip+ni*nip+i] = yc[i];                     /* 1S -> 6N */
    xc[2*nip*nip+i]        = xc[nip*nip+ni*nip+i];      /* 2N -> 3S */
    yc[2*nip*nip+i]        = yc[nip*nip+ni*nip+i];      /* 2N -> 3S */
    xc[3*nip*nip+i]        = xc[nip*nip+(ni-i)*nip+ni];  /* 2E -> 4S */
    yc[3*nip*nip+i]        = yc[nip*nip+(ni-i)*nip+ni];  /* 2E -> 4S */
  }
  for(j=0; j<nip; j++) {
    xc[5*nip*nip+j*nip+ni] = xc[nip*nip+ni-j];          /* 2S -> 6E */
    yc[5*nip*nip+j*nip+ni] = yc[nip*nip+ni-j];          /* 2S -> 6E */
    xc[3*nip*nip+j*nip]    = xc[2*nip*nip+j*nip+ni];    /* 3E -> 4W */
    yc[3*nip*nip+j*nip]    = yc[2*nip*nip+j*nip+ni];    /* 3E -> 4W */
    xc[4*nip*nip+j*nip]    = xc[2*nip*nip+ni*nip+ni-j]; /* 3N -> 5W */
    yc[4*nip*nip+j*nip]    = yc[2*nip*nip+ni*nip+ni-j]; /* 3N -> 5W */
  }
  for(i=0; i<nip; i++) {
    xc[4*nip*nip+i] = xc[3*nip*nip+ni*nip+i];           /* 4N -> 5S */
    yc[4*nip*nip+i] = yc[3*nip*nip+ni*nip+i];           /* 4N -> 5S */
    xc[5*nip*nip+i] = xc[3*nip*nip+(ni-i)*nip+ni];      /* 4E -> 6S */
    yc[5*nip*nip+i] = yc[3*nip*nip+(ni-i)*nip+ni];      /* 4E -> 6S */
  }
  for(j=0; j<nip; j++) {
    xc[5*nip*nip+j*nip] = xc[4*nip*nip+j*nip+ni];    /* 5E -> 6W */
    yc[5*nip*nip+j*nip] = yc[4*nip*nip+j*nip+ni];    /* 5E -> 6W */
  }

  /* Schmidt transformation */
  if ( do_schmidt ) {
    for(n=0; n<ntiles; n++) {
      direct_transform(stretch_factor, 0, ni, 0, ni, target_lon*D2R, target_lat*D2R,
                       n, xc+n*nip*nip, yc+n*nip*nip);
    }
  }
  else if( do_cube_transform ) {
    for(n=0; n<ntiles; n++) {
      cube_transform(stretch_factor, 0, ni, 0, ni, target_lon*D2R, target_lat*D2R,
                     n, xc+n*nip*nip, yc+n*nip*nip);
    }
  }


  /* get nest grid */
  if(global_nest) {
    fprintf(stderr, "[INFO] pre-callling setup_aligned_nest_GR, nip=%ld\n", nip);
    npts = ntiles*nip*nip;
    xc2 = (double *)malloc(npts*sizeof(double));
    yc2 = (double *)malloc(npts*sizeof(double));
    for(n=0; n<npts; n++) {
      xc2[n] = xc[n];
      yc2[n] = yc[n];
    }
    free(xc);
    free(yc);
    ni2  = ni;
    ni2p = nip;
    ni   = nx/2;
    nip  = ni + 1;
    npts = ntiles*nip*nip;
    xc = (double *)malloc(npts*sizeof(double));
    yc = (double *)malloc(npts*sizeof(double));
    for(n=0; n<ntiles; n++) {
      fprintf(stderr, "[INFO] callling setup_aligned_nest_GR,xc nip=%ld n=%ld\n",nip, n);
      int offset_p = ni2p*ni2p*n;
      setup_aligned_nest(ni2, ni2,xc2+offset_p, yc2+offset_p, 0, refine_ratio,
                            1, ni2, 1, ni2, xc+n*nip*nip, yc+n*nip*nip, 1 );
    }
  }
  else{
    mpp_error ("Using function intended for global refinement only");
  }

  /* calculate grid box center location */

  ni2 = 0;
  nj2 = 0;
  for(n=0; n<ntiles2; n++) {
    if(nil[n]>ni2) ni2 = nil[n];
    if(njl[n]>nj2) nj2 = njl[n];
  }
  ni2p = ni2+1;
  nj2p = nj2+1;
  xtmp = (double *)malloc(ni2p*nj2p*sizeof(double));
  ytmp = (double *)malloc(ni2p*nj2p*sizeof(double));

  for(n=0; n<ntiles2; n++) {
    long n1,n2;
    /* copy C-cell to supergrid */
    for(j=0; j<=njl[n]; j++) for(i=0; i<=nil[n]; i++) { //MZ L1
        n1 = n*nxp*nxp+j*2*(2*nil[n]+1)+i*2;
        n2 = n*nip*nip+j*(nil[n]+1)+i;
        x[n1]=xc[n2];
        y[n1]=yc[n2];
      }

    /* cell center and copy to super grid */
    cell_center(nil[n], njl[n], xc+n*nip*nip, yc+n*nip*nip, xtmp, ytmp);
    for(j=0; j<njl[n]; j++) for(i=0; i<nil[n]; i++) { //MZ L2
        n1 = n*nxp*nxp+(j*2+1)*(2*nil[n]+1)+i*2+1;
        n2 = j*nil[n]+i;
        x[n1]=xtmp[n2];
        y[n1]=ytmp[n2];
      }

    /* cell east and copy to super grid */
    cell_east(nil[n], njl[n], xc+n*nip*nip, yc+n*nip*nip, xtmp, ytmp);
    for(j=0; j<njl[n]; j++) for(i=0; i<=nil[n]; i++) { //MZ L3
        n1 = n*nxp*nxp+(j*2+1)*(2*nil[n]+1)+i*2;
        n2 = j*(nil[n]+1)+i;
        x[n1]=xtmp[n2];
        y[n1]=ytmp[n2];
      }

    /* cell north and copy to super grid */
    cell_north(nil[n], njl[n], xc+n*nip*nip, yc+n*nip*nip, xtmp, ytmp);
    for(j=0; j<=njl[n]; j++) for(i=0; i<nil[n]; i++) { //MZ L4
        n1 = n*nxp*nxp+(j*2)*(2*nil[n]+1)+i*2+1;
        n2 = j*nil[n]+i;
        x[n1]=xtmp[n2];
        y[n1]=ytmp[n2];
      }
  }

  free(xtmp);
  free(ytmp);

  /* calculate grid cell length */
  if(output_length_angle) {
    for(n=0; n<ntiles2; n++) {
      long n1, n2, n0;
      for(j=0; j<=nyl[n]; j++) {
        for(i=0; i<nxl[n]; i++) {
          n0 = n*nx*nxp+j*nxl[n]+i;
          n1 = n*nxp*nxp+j*(nxl[n]+1)+i;
          n2 = n*nxp*nxp+j*(nxl[n]+1)+i+1;

          p1[0] = x[n1];
          p1[1] = y[n1];
          p2[0] = x[n2];
          p2[1] = y[n2];
          dx[n0] = great_circle_distance(p1, p2);
        }
      }
    }
    for(n=0; n<ntiles2; n++) {
      long n1, n2, n0;
      if( stretched_grid || n==ntiles ) {
        for(j=0; j<nyl[n]; j++) {
          for(i=0; i<=nxl[n]; i++) {
            n0 = n*nx*nxp+j*(nxl[n]+1)+i;
            n1 = n*nxp*nxp+j*(nxl[n]+1)+i;
            n2 = n*nxp*nxp+(j+1)*(nxl[n]+1)+i;
            p1[0] = x[n1];
            p1[1] = y[n1];
            p2[0] = x[n2];
            p2[1] = y[n2];
            dy[n*nx*nxp+j*(nxl[n]+1)+i] = great_circle_distance(p1, p2);
          }
        }
      }
      else {
        long n1, n2;
        for(n=0; n<ntiles; n++) {
          for(j=0; j<nyp; j++) {
            for(i=0; i<nx; i++) {
              n1 = n*nx*nxp+i*nxp+j;
              n2 = n*nx*nxp+j*nx+i;
              dy[n1] = dx[n2];
            }
          }
        }
      }
    }

    /* ensure consistency on the boundaries between tiles */
    for(j=0; j<nx; j++) {
      long n11, n21, n31, n41, n51, n61, n71, n81, n91;
      long n12, n22, n32, n42, n52, n62, n72, n82, n92;
      n11 = j*nxp;
      n12 = 4*nx*nxp+nx*nx+nx-j-1;
      n21 = j*nxp+nx;
      n22 = nxp*nx+j*nxp;
      n31 = nxp*nx+j*nxp+nx;
      n32 = 3*nx*nxp+(nx-j-1);
      n41 = 2*nxp*nx+j*nxp;
      n42 = nx*nx+nx-j-1;
      n51 = 2*nxp*nx+j*nxp+nx;
      n52 = 3*nxp*nx+j*nxp;
      n61 = 3*nxp*nx+j*nxp+nx;
      n62 = 5*nx*nxp+(nx-j-1);
      n71 = 4*nxp*nx+j*nxp;
      n72 = 2*nx*nxp+nx*nx+nx-j-1;
      n81 = 4*nxp*nx+j*nxp+nx;
      n82 = 5*nxp*nx+j*nxp;
      n91= 5*nxp*nx+j*nxp+nx;
      n92 = nx*nxp+(nx-j-1);
      dy[n11] = dx[n12]; /* 5N -> 1W */
      dy[n21] = dy[n22]; /* 2W -> 1E */
      dy[n31] = dx[n32]; /* 4S -> 2E */
      dy[n41] = dx[n42]; /* 1N -> 3W */
      dy[n51] = dy[n52]; /* 4W -> 3E */
      dy[n61] = dx[n62]; /* 4S -> 2E */
      dy[n71] = dx[n72]; /* 3N -> 5W */
      dy[n81] = dy[n82]; /* 6W -> 5E */
      dy[n91] = dx[n92]; /* 2S -> 6E */
    }
  }

  {
    long pos1, pos2;
    pos1 = 0;
    pos2 = 0;
    if ( do_schmidt || do_cube_transform) {
      for(n=0; n<ntiles; n++) {
        calc_cell_area(nx, ny, x+pos1, y+pos1, area+pos2);
        pos1 += (nx+1)*(nx+1);
        pos2 += nx*nx;
      }
    }else {
      calc_cell_area(nx, ny, x, y, area);
      for(j=0; j<nx; j++) {
        long n0, n1, n2, n3, n4, n5;
        for(i=0; i<nx; i++) {
          double ar;
          /* all the face have the same area */
          n0 = j*nx+i;
          n1 = nx*nx+j*nx+i;
          n2 = 2*nx*nx+j*nx+i;
          n3 = 3*nx*nx+j*nx+i;
          n4 = 4*nx*nx+j*nx+i;
          n5 = 5*nx*nx+j*nx+i;
          ar = area[n0];
          area[n1] = ar;
          area[n2] = ar;
          area[n3] = ar;
          area[n4] = ar;
          area[n5] = ar;
        }
      }
    }


    /* calculate nested grid area */
    if(ntiles2>ntiles) {
      pos1 = ntiles*nxp*nyp;
      pos2 = ntiles*nx*ny;
      calc_cell_area(nx_nest, ny_nest, x+pos1, y+ntiles*nxp*nyp, area+pos2);
    }
  }

  if(output_length_angle) {
    /*calculate rotation angle, just some workaround, will modify this in the future. */
    calc_rotation_angle2(nxp, x, y, angle_dx, angle_dy );

    /* since angle is used in the model, set angle to 0 for nested region */
    if(ntiles2>ntiles) {
      for(i=0; i<=(nx_nest+1)*(ny_nest+1); i++) {
        angle_dx[ntiles*nxp*nxp+i]=0;
        angle_dy[ntiles*nxp*nxp+i]=0;
      }
    }
  }

  {
    long npts, i;
    /* convert grid location from radians to degree */
    npts = ntiles*nxp*nyp;
    if(nx_nest>0) npts += (nx_nest+1)*(ny_nest+1);


    for(i=0; i<npts; i++) {
      x[i] = x[i]*R2D;
      y[i] = y[i]*R2D;
    }
  }
  free(xc);
  free(yc);
  free(nxl);
  free(nyl);
  free(nil);
  free(njl);
  free(lon);
  free(lat);
  free(xc2);
  free(yc2);

}

void calc_cell_area(int nx, int ny, const double *x, const double *y, double *area)
{
  int i,j, nxp;
  double p_ll[2], p_ul[2], p_lr[2], p_ur[2];

  nxp = nx+1;
  for(j=0; j<ny; j++) {
    for(i=0; i<nx; i++) {
      p_ll[0] = x[j*nxp+i];       p_ll[1] = y[j*nxp+i];
      p_ul[0] = x[(j+1)*nxp+i];   p_ul[1] = y[(j+1)*nxp+i];
      p_lr[0] = x[j*nxp+i+1];     p_lr[1] = y[j*nxp+i+1];
      p_ur[0] = x[(j+1)*nxp+i+1]; p_ur[1] = y[(j+1)*nxp+i+1];
      /* all the face have the same area */
      area[j*nx+i] = spherical_excess_area(p_ll, p_ul, p_lr, p_ur, RADIUS);
    }
  }

}



/*-------------------------------------------------------------------------
  void direct_transform(double c, int i1, int i2, int j1, int j2, double lon_p, double lat_p, int n,
  double *lon, double *lat)

  This is a direct transformation of the standard (symmetrical) cubic grid
  to a locally enhanced high-res grid on the sphere; it is an application
  of the Schmidt transformation at the south pole followed by a
  pole_shift_to_target (rotation) operation

  arguments:
  c            : Stretching factor
  lon_p, lat_p : center location of the target face, radian
  n            : grid face number
  i1,i2,j1,j2  : starting and ending index in i- and j-direction
  lon          : longitude. 0 <= lon <= 2*pi
  lat          : latitude. -pi/2 <= lat <= pi/2
  ------------------------------------------------------------------------*/

void direct_transform(double stretch_factor, int i1, int i2, int j1, int j2, double lon_p, double lat_p,
                      int n, double *lon, double *lat)
{
#ifndef HAVE_LONG_DOUBLE_WIDER
  double lat_t, sin_p, cos_p, sin_lat, cos_lat, sin_o, p2, two_pi;
  double c2p1, c2m1;
#else
  long double lat_t, sin_p, cos_p, sin_lat, cos_lat, sin_o, p2, two_pi;
  long double c2p1, c2m1;
#endif
  int i, j, l, nxp;

  nxp = i2-i1+1;
  p2 = 0.5*M_PI;
  two_pi = 2.*M_PI;
  if(n==0) printf("create_gnomonic_cubic_grid: Schmidt transformation: stretching factor=%g, center=(%g,%g)\n",
                  stretch_factor, lon_p, lat_p);

  c2p1 = 1. + stretch_factor*stretch_factor;
  c2m1 = 1. - stretch_factor*stretch_factor;

  sin_p = sin(lat_p);
  cos_p = cos(lat_p);

  for(j=j1; j<=j2; j++) for(i=i1; i<=i2; i++) {
      l = j*nxp+i;
      if ( fabs(c2m1) > EPSLN7 ) {
        sin_lat = sin(lat[l]);
        lat_t   = asin( (c2m1+c2p1*sin_lat)/(c2p1+c2m1*sin_lat) );
      }
      else {
        lat_t = lat[l];
      }
      sin_lat = sin(lat_t);
      cos_lat = cos(lat_t);
      sin_o = -(sin_p*sin_lat + cos_p*cos_lat*cos(lon[l]));
      if ( (1.-fabs(sin_o)) < EPSLN7 ) {    /* poles */
        lon[l] = 0.;
        lat[l] = (sin_o < 0) ? -p2:p2;
      }
      else {
        lat[l] = asin( sin_o );
        lon[l] = lon_p + atan2(-cos_lat*sin(lon[l]), -sin_lat*cos_p+cos_lat*sin_p*cos(lon[l]));
        if ( lon[l] < 0. )
          lon[l] +=two_pi;
        else if( lon[l] >= two_pi )
          lon[l] -=two_pi;
      }
    }
} /* direct_transform */

/*
  void suggest_target_lats(double stretch_factor, int i1, int i2, int j1, int j2, double lon_p, double lat_p, int ntiles,
                         double *lon, double *lat)

  This subroutine suggests values for target latitude close to the desired ones
  so that the stretched grid would include the North pole and/or the South pole as grid points.

  South pole is a fixed point of the stretching transformation:
       inter_lat   = asin( (c2m1+c2p1*sin(init_lat))/(c2p1+c2m1*sin(init_lat)) );
  After stretching the intermediate grid is rotated so that the South pole
  shifts to the target point of the final stretched grid:
       final_latitude = -asin(sin_p*sin(inetr_lat) + cos_p*cos(inter_lat)*cos(init_lon[l]));
       final_longitude= lon_p + atan(-cos(inter_lat)*sin(init_lon) / -sin(inter_lat)*cos_p+cos(inter_lat)*sin_p*cos(init_lon));
  Generally for a given target latitude the final grid will not have the N or S poles (they are not rotated into grid points).
  But it is possible to restrict the final grid to include one or both poles by slightly adjusting the target latitude.
  In the generating algorithm the intermediate grid is roateted by 90+lat_p, to shift the intemediate South pole to the target point.
  Hence the intermediate point with (lon,lat)=(180,-lat_p) would rotate to the North pole and (lon,lat)=(180,180-lat_p) would rotate to the South pole.
  So if such points are in the intermediate grid they would generate the N&S poles in the final grid.
  There is no guarantee that (180,-lat_p) with arbitrary lat_p would be in the intermedaite grid.
  But, we can adjust lat_p a little to have the pre-image of the North pole in the intermedaite grid.
  We first find the latitude of the pre-image in the inital grid by inverting the formula for the stretch transformation:
       lam_North_pre=-asin((c2m1-c2p1*sin_p)/(c2p1-c2m1*sin_p))
  Then we find the closest point in the initial grid  with (lon,lat)=(180,lam_North_pre)
  Then we find the target point latitude that would generate the pre-image of North pole in intermediate grid.
  A similar formula applies to generate the South pole.
  To have both poles as grid points an intermediate value for target can be found so that both N&S conditions hold.
*/
void suggest_target_lats(double stretch_factor, int i1, int i2, int j1, int j2, double lon_p, double lat_p, int ntiles,
                         double *lon, double *lat)
{
#ifndef HAVE_LONG_DOUBLE_WIDER
  double lat_t, sin_p, cos_p, sin_lat, cos_lat, sin_o, p2, two_pi;
  double c2p1, c2m1;
#else
  long double lat_t, sin_p, cos_p, sin_lat, cos_lat, sin_o, p2, two_pi;
  long double c2p1, c2m1;
#endif
  int i, j, l, nxp, n, nip,jn,js,is,in,ln,ls;
  double lam_South_pre,lam_North_pre,adjusted_r,r4,sTN,sN,sTS,sS;
  double adjusted_target_latN=-99.,adjusted_target_latS=-99.,f,b;
  int NPtile=-1,NPi=-1,NPj=-1,SPtile=-1,SPi=-1,SPj=-1;

  nxp = i2-i1+1;
  nip = i2+1;
  c2p1 = 1. + stretch_factor*stretch_factor;
  c2m1 = 1. - stretch_factor*stretch_factor;
  sin_p = sin(lat_p);
  cos_p = cos(lat_p);
  printf("Input target latitude: %g\n",R2D*lat_p);
  //North pole adjustment?
  //find the latitude of the pre-image in the inital grid by inverting the formula for the stretch transformation
  lam_North_pre=-asin((c2m1+c2p1*sin_p)/(c2p1+c2m1*sin_p));
  lam_South_pre=-asin((c2m1-c2p1*sin_p)/(c2p1-c2m1*sin_p));

  for(n=0; n<ntiles; n++) {
    //find the closest point in the initial grid  with (lon,lat)=(180,lam_North_pre)
    for(j=j1; j<=j2; j++) for(i=i1; i<=i2; i++) {
	l = n*nip*nip + j*nxp+i;
	if(fabs(lon[l]-M_PI)<0.00010 & fabs(lat[l]-lam_North_pre)<0.0050){
	  NPtile = n;
	  NPj = j;
	  NPi = i;
          //find the target point latitude that would generate the pre-image of North pole in intermediate grid.
          adjusted_target_latN = -asin((c2m1+c2p1*sin(lat[l]))/(c2p1+c2m1*sin(lat[l])));
          printf("Suggested target latitude to have the North pole in the grid: %g\n",R2D*adjusted_target_latN);
	  //printf("FoundN: %d,%d,%d,%g,%g,%g\n",NPtile,NPj,l,R2D*lon[l],R2D*lat[l],R2D*lam_North_pre);
          break;
	}
      }
    //South pole adjustment?
    for(j=j1; j<=j2; j++) for(i=i1; i<=i2; i++) {
	l = n*nip*nip + j*nxp+i;
	if(fabs(lon[l]-M_PI)<0.00010 & fabs(lat[l]-lam_South_pre)<0.0050){
	  SPtile = n;
	  SPj = j;
	  SPi = i;
          adjusted_target_latS = asin((c2m1+c2p1*sin(lat[l]))/(c2p1+c2m1*sin(lat[l])));
          printf("Suggested target latitude to have the South pole in the grid: %g\n",R2D*adjusted_target_latS);
	  //printf("FoundS: %d,%d,%d,%g,%g,%g\n",SPtile,SPj,l,R2D*lon[l],R2D*lat[l],R2D*lam_South_pre);
          break;
	}
      }
  }
  //printf("NPtile ,i,j: %d,%d,%d\n",NPtile,NPi,NPj);
  //printf("SPtile ,i,j: %d,%d,%d\n",SPtile,SPi,SPj);

  /*
    In the following f=b is the condition that could generate both N & S poles in the final grid
    for a given stretch factor. We search the initial grid points near what we found previously for
    N and S separately to find a suitable target latitude so that the final grid includes both poles.
  */
  f=(c2p1/c2m1 + c2m1/c2p1);
  for(in=NPi-10; in<=NPi+10; in++) {
     for(is=SPi-10; is<=SPi+10; is++) {
	  ln = NPtile*nip*nip + NPj*nxp+in;
	  ls = SPtile*nip*nip + SPj*nxp+is;
	  b = -2*(1.0+sin(lat[ln])*sin(lat[ls]))/(sin(lat[ln])+sin(lat[ls]));
	  if(fabs(f-b)<0.0001){
	    sS=sin(lat[ls]);
	    sTS= (c2m1+c2p1*sS)/(c2p1+c2m1*sS);
	    adjusted_target_latS = asin(sTS);
	    printf("Suggested target latitude to have both North and South poles in the grid: %g\n",R2D*adjusted_target_latS);
	    /* similarly
	    sN=sin(lat[ln]);
	    sTN=-(c2m1+c2p1*sN)/(c2p1+c2m1*sN);
	    adjusted_target_latN = asin(sTN);
	    but adjusted_target_latN should equal adjusted_target_latS and does not yield more info.
	    */
	    //printf("FoundSPj: %d,%d,%g,%g,%g\n",in,is,fabs(f-b),R2D*adjusted_target_latN,R2D*adjusted_target_latS);
  	  }
     }
  }
} /*suggest_target_lats*/

/*-------------------------------------------------------------------------
  void cube_transform(double c, int i1, int i2, int j1, int j2, double lon_p, double lat_p, int n,
  double *lon, double *lat)

  This is a direct transformation of the standard (symmetrical) cubic grid
  to a locally enhanced high-res grid on the sphere; it is an application
  of the Schmidt transformation at the **north** pole followed by a
  pole_shift_to_target (rotation) operation

  arguments:
  c            : Stretching factor
  lon_p, lat_p : center location of the target face, radian
  n            : grid face number
  i1,i2,j1,j2  : starting and ending index in i- and j-direction
  lon          : longitude. 0 <= lon <= 2*pi
  lat          : latitude. -pi/2 <= lat <= pi/2
  ------------------------------------------------------------------------*/

void cube_transform(double stretch_factor, int i1, int i2, int j1, int j2, double lon_p, double lat_p,
                    int n, double *lon, double *lat)
{
#ifndef HAVE_LONG_DOUBLE_WIDER
  double lat_t, sin_p, cos_p, sin_lat, cos_lat, sin_o, p2, two_pi;
  double c2p1, c2m1;
#else
  long double lat_t, sin_p, cos_p, sin_lat, cos_lat, sin_o, p2, two_pi;
  long double c2p1, c2m1;
#endif
  int i, j, l, nxp;

  nxp = i2-i1+1;
  p2 = 0.5*M_PI;
  two_pi = 2.*M_PI;
  if(n==0) printf("create_gnomonic_cubic_grid: Cube transformation (revised Schmidt): stretching factor=%g, center=(%g,%g)\n",
                  stretch_factor, lon_p, lat_p);

  c2p1 = 1. + stretch_factor*stretch_factor;
  c2m1 = 1. - stretch_factor*stretch_factor;

  sin_p = sin(lat_p);
  cos_p = cos(lat_p);
  /* Try rotating pole around before doing the regular rotation */
  for(j=j1; j<=j2; j++) for(i=i1; i<=i2; i++) {
      l = j*nxp+i;
      if ( fabs(c2m1) > EPSLN7 ) {
        sin_lat = sin(lat[l]);
        lat_t   = asin( (c2m1+c2p1*sin_lat)/(c2p1+c2m1*sin_lat) );
      }
      else {
        lat_t = lat[l];
      }
      sin_lat = sin(lat_t);
      cos_lat = cos(lat_t);
      lon[l] = lon[l] + M_PI; /* rotate around first to get final orientation correct */
      sin_o = -(sin_p*sin_lat + cos_p*cos_lat*cos(lon[l]));
      if ( (1.-fabs(sin_o)) < EPSLN7 ) {    /* poles */
        lon[l] = 0.;
        lat[l] = (sin_o < 0) ? -p2:p2;
      }
      else {
        lat[l] = asin( sin_o );
        lon[l] = lon_p + atan2(-cos_lat*sin(lon[l]), -sin_lat*cos_p+cos_lat*sin_p*cos(lon[l]));
        if ( lon[l] < 0. )
          lon[l] +=two_pi;
        else if( lon[l] >= two_pi )
          lon[l] -=two_pi;
      }
    }

} /* cube_transform */

/*-----------------------------------------------------
  void gnomonic_ed
  Equal distance along the 4 edges of the cubed sphere
  -----------------------------------------------------
  Properties:
  * defined by intersections of great circles
  * max(dx,dy; global) / min(dx,dy; global) = sqrt(2) = 1.4142
  * Max(aspect ratio) = 1.06089
  * the N-S coordinate curves are const longitude on the 4 faces with equator
  For C2000: (dx_min, dx_max) = (3.921, 5.545)    in km unit
  ! Ranges:
  ! lamda = [0.75*pi, 1.25*pi]
  ! theta = [-alpha, alpha]
  --------------------------------------------------------*/
void gnomonic_ed(int ni, double* lamda, double* theta)
{

  int i, j, n, nip;
  double dely;
  double *x, *y, *z;
  double rsq3, alpha;


  nip = ni + 1;
  rsq3 = 1./sqrt(3.);
  alpha = asin( rsq3 );

  dely = 2.*alpha/ni;

  /* Define East-West edges: */
  for(j=0; j<nip; j++) {
    lamda[j*nip]    = 0.75*M_PI;               /* West edge */
    lamda[j*nip+ni] = 1.25*M_PI;               /* East edge */
    theta[j*nip]    = -alpha + dely*j;       /* West edge */
    theta[j*nip+ni] = theta[j*nip];          /* East edge */
  }

  /* Get North-South edges by symmetry: */

  for(i=1; i<ni; i++) {
    mirror_latlon(lamda[0], theta[0], lamda[ni*nip+ni], theta[ni*nip+ni],
                  lamda[i*nip], theta[i*nip], &lamda[i], &theta[i] );
    lamda[ni*nip+i] = lamda[i];
    theta[ni*nip+i] = -theta[i];
  }

  x = (double *)malloc(nip*nip*sizeof(double));
  y = (double *)malloc(nip*nip*sizeof(double));
  z = (double *)malloc(nip*nip*sizeof(double));
  /* Set 4 corners: */
  latlon2xyz(1, &lamda[0], &theta[0], &x[0], &y[0], &z[0]);
  latlon2xyz(1, &lamda[ni], &theta[ni], &x[ni], &y[ni], &z[ni]);
  latlon2xyz(1, &lamda[ni*nip], &theta[ni*nip], &x[ni*nip], &y[ni*nip], &z[ni*nip]);
  latlon2xyz(1, &lamda[ni*nip+ni], &theta[ni*nip+ni], &x[ni*nip+ni], &y[ni*nip+ni], &z[ni*nip+ni]);

  /* Map edges on the sphere back to cube:
     Intersections at x=-rsq3   */

  for(j=1; j<ni; j++) {
    n = j*nip;
    latlon2xyz(1, &lamda[n], &theta[n], &x[n], &y[n], &z[n]);
    y[n] = -y[n]*rsq3/x[n];
    z[n] = -z[n]*rsq3/x[n];
  }

  for(i=1; i<ni; i++) {
    latlon2xyz(1, &lamda[i], &theta[i], &x[i], &y[i], &z[i]);
    y[i] = -y[i]*rsq3/x[i];
    z[i] = -z[i]*rsq3/x[i];
  }

  for(j=0; j<nip; j++)
    for(i=0; i<nip; i++) x[j*nip+i] = -rsq3;

  for(j=1;j<nip; j++) {
    for(i=1; i<nip; i++) {
      y[j*nip+i] = y[i];
      z[j*nip+i] = z[j*nip];
    }
  }

  xyz2latlon(nip*nip, x, y, z, lamda, theta);

  free(x);
  free(y);
  free(z);

} /* gnomonic_ed */

/*----------------------------------------------------------
  void gnomonic_angl()
  This is the commonly known equi-angular grid
  //TODO: Implement this function?
  **************************************************************/

void gnomonic_angl(int ni, double* lamda, double* theta)
{
  mpp_error("function gnomonic_angl not yet implemented");
}/* gnomonic_angl */

/*----------------------------------------------------------
  void gnomonic_dist()
  This is the commonly known equi-distance grid
  //TODO: Implement this function?
  **************************************************************/

void gnomonic_dist(int ni, double* lamda, double* theta)
{
  mpp_error("function gnomonic_dist not yet implemented");


} /* gnomonic_dist */


/*------------------------------------------------------------------
  void mirror_latlon
  Given the "mirror" as defined by (lon1, lat1), (lon2, lat2), and center
  of the sphere, compute the mirror image of (lon0, lat0) as  (lon, lat)
  ---------------------------------------------------------------*/

void mirror_latlon(double lon1, double lat1, double lon2, double lat2, double lon0,
                   double lat0, double *lon, double *lat)
{
  double p0[3], p1[3], p2[3], pp[3], nb[3];
  double pdot;
  int k;

  latlon2xyz(1, &lon0, &lat0, &p0[0], &p0[1], &p0[2]);
  latlon2xyz(1, &lon1, &lat1, &p1[0], &p1[1], &p1[2]);
  latlon2xyz(1, &lon2, &lat2, &p2[0], &p2[1], &p2[2]);
  vect_cross(p1, p2, nb);

  pdot = sqrt(nb[0]*nb[0]+nb[1]*nb[1]+nb[2]*nb[2]);
  for(k=0; k<3; k++) nb[k] = nb[k]/pdot;

  pdot = p0[0]*nb[0] + p0[1]*nb[1] + p0[2]*nb[2];
  for(k=0; k<3; k++) pp[k] = p0[k] - 2*pdot*nb[k];
  xyz2latlon(1, &pp[0], &pp[1], &pp[2], lon, lat);

} /* mirror_latlon */

/*-------------------------------------------------------------------------
  void symm_ed(int ni, double *lamda, double *theta)
  ! Make grid symmetrical to i=ni/2+1 and j=nj/2+1
  ------------------------------------------------------------------------*/
void symm_ed(int ni, double *lamda, double *theta)
{

  int nip, i, j, ip, jp;
  double avg;

  nip = ni+1;

  for(j=1; j<nip; j++)
    for(i=1; i<ni; i++) lamda[j*nip+i] = lamda[i];

  for(j=0; j<nip; j++) {
    for(i=0; i<ni/2; i++) {
      ip = ni - i;
      avg = 0.5*(lamda[j*nip+i]-lamda[j*nip+ip]);
      lamda[j*nip+i] = avg + M_PI;
      lamda[j*nip+ip] = M_PI - avg;
      avg = 0.5*(theta[j*nip+i]+theta[j*nip+ip]);
      theta[j*nip+i] = avg;
      theta[j*nip+ip] = avg;
    }
  }

  /* Make grid symmetrical to j=im/2+1 */
  for(j = 0; j<ni/2; j++) {
    jp = ni - j;
    for(i=1; i<ni; i++) {
      avg = 0.5*(lamda[j*nip+i]+lamda[jp*nip+i]);
      lamda[j*nip+i] = avg;
      lamda[jp*nip+i] = avg;
      avg = 0.5*(theta[j*nip+i]-theta[jp*nip+i]);
      theta[j*nip+i] = avg;
      theta[jp*nip+i] = -avg;
    }
  }
}/* symm_ed */

/*------------------------------------------------------------------------------
  void mirror_grid( )
  Mirror Across the 0-longitude
  ----------------------------------------------------------------------------*/
void mirror_grid(int ni, int ntiles, double *x, double *y )
{
  int nip, i, j, ip, jp, nt;
  double x1, y1, z1, x2, y2, z2, ang;

  nip = ni+1;

  for(j=0; j<ceil(nip/2.); j++) {
    jp = ni - j;
    for(i=0; i<ceil(nip/2.); i++) {
      ip = ni - i;
      x1 = 0.25 * (fabs(x[j*nip+i]) + fabs(x[j*nip+ip]) + fabs(x[jp*nip+i]) + fabs(x[jp*nip+ip]) );
      x[j*nip+i]   = x1 * (x[j*nip+i]   >=0 ? 1:-1);
      x[j*nip+ip]  = x1 * (x[j*nip+ip]  >=0 ? 1:-1);
      x[jp*nip+i]  = x1 * (x[jp*nip+i]  >=0 ? 1:-1);
      x[jp*nip+ip] = x1 * (x[jp*nip+ip] >=0 ? 1:-1);

      y1 = 0.25 * (fabs(y[j*nip+i]) + fabs(y[j*nip+ip]) + fabs(y[jp*nip+i]) + fabs(y[jp*nip+ip]) );
      y[j*nip+i]   = y1 * (y[j*nip+i]   >=0 ? 1:-1);
      y[j*nip+ip]  = y1 * (y[j*nip+ip]  >=0 ? 1:-1);
      y[jp*nip+i]  = y1 * (y[jp*nip+i]  >=0 ? 1:-1);
      y[jp*nip+ip] = y1 * (y[jp*nip+ip] >=0 ? 1:-1);

      /* force dateline/greenwich-meridion consitency */
      if( nip%2 ) {
        if( i == (nip-1)/2 ) {
          x[j*nip+i] = 0.0;
          x[jp*nip+i] = 0.0;
        }
      }
    }
  }

  /* define the the other five tiles. */
  for(nt=1; nt<ntiles; nt++) {
    for(j=0; j<nip; j++) {
      for(i=0; i<nip; i++) {
        x1 = x[j*nip+i];
        y1 = y[j*nip+i];
        z1 = RADIUS;
        switch (nt) {
        case 1: /* tile 2 */
          ang = -90.;
          rot_3d( 3, x1, y1, z1, ang, &x2, &y2, &z2, 1, 1);  /* rotate about the z-axis */
          break;
        case 2: /* tile 3 */
          ang = -90.;
          rot_3d( 3, x1, y1, z1, ang, &x2, &y2, &z2, 1, 1);  /* rotate about the z-axis */
          ang = 90.;
          rot_3d( 1, x2, y2, z2, ang, &x1, &y1, &z1, 1, 1); /* rotate about the z-axis */
          x2=x1;
          y2=y1;
          z2=z1;

          /* force North Pole and dateline/greenwich-meridion consitency */
          if(nip%2) {
            if( (i==(nip-1)/2) && (i==j) ) {
              x2 = 0;
              y2 = M_PI*0.5;
            }

            if( (j==(nip-1)/2) && (i<(nip-1)/2) ) x2 = 0;
            if( (j==(nip-1)/2) && (i>(nip-1)/2) ) x2 = M_PI;
          }
          break;
        case 3: /* tile 4 */
          ang = -180.;
          rot_3d( 3, x1, y1, z1, ang, &x2, &y2, &z2, 1, 1); /* rotate about the z-axis */
          ang = 90.;
          rot_3d( 1, x2, y2, z2, ang, &x1, &y1, &z1, 1, 1); /* rotate about the z-axis */
          x2=x1;
          y2=y1;
          z2=z1;

          /* force dateline/greenwich-meridion consitency */
          if( nip%2 ) {
            if( j == (nip-1)/2 ) x2 = M_PI;
          }
          break;
        case 4: /* tile 5 */
          ang = 90.;
          rot_3d( 3, x1, y1, z1, ang, &x2, &y2, &z2, 1, 1); /* rotate about the z-axis */
          ang = 90.;
          rot_3d( 2, x2, y2, z2, ang, &x1, &y1, &z1, 1, 1); /* rotate about the z-axis */
          x2=x1;
          y2=y1;
          z2=z1;
          break;
        case 5: /* tile 6 */
          ang = 90.;
          rot_3d( 2, x1, y1, z1, ang, &x2, &y2, &z2, 1, 1); /* rotate about the z-axis */
          ang = 0.;
          rot_3d( 3, x2, y2, z2, ang, &x1, &y1, &z1, 1, 1); /* rotate about the z-axis */
          x2=x1;
          y2=y1;
          z2=z1;

          /* force South Pole and dateline/greenwich-meridion consitency */
          if(nip%2) {
            if( (i==(nip-1)/2) && (i==j) ) {
              x2 = 0;
              y2 = -M_PI*0.5;
            }

            if( (i==(nip-1)/2) && (j>(nip-1)/2) ) x2 = 0;
            if( (i==(nip-1)/2) && (j<(nip-1)/2) ) x2 = M_PI;
          }
          break;
        }
        x[nt*nip*nip+j*nip+i] = x2;
        y[nt*nip*nip+j*nip+i] = y2;
      }
    }
  }
} /* mirror_grid */


/*-------------------------------------------------------------------------------
  void rot_3d()
  rotate points on a sphere in xyz coords (convert angle from
  degrees to radians if necessary)
  -----------------------------------------------------------------------------*/
void rot_3d(int axis, double x1in, double y1in, double z1in, double angle, double *x2out,
            double *y2out, double *z2out, int degrees, int convert)
{

  double x1, y1, z1, x2, y2, z2, c, s;

  if(convert)
    spherical_to_cartesian(x1in, y1in, z1in, &x1, &y1, &z1);
  else {
    x1=x1in;
    y1=y1in;
    z1=z1in;
  }

  if(degrees) angle = angle*D2R;

  c = cos(angle);
  s = sin(angle);

  switch (axis) {
  case 1:
    x2 =  x1;
    y2 =  c*y1 + s*z1;
    z2 = -s*y1 + c*z1;
    break;
  case 2:
    x2 = c*x1 - s*z1;
    y2 = y1;
    z2 = s*x1 + c*z1;
    break;
  case 3:
    x2 =  c*x1 + s*y1;
    y2 = -s*x1 + c*y1;
    z2 = z1;
    break;
  default:
    mpp_error("Invalid axis: must be 1 for X, 2 for Y, 3 for Z.");
  }

  if(convert)
    cartesian_to_spherical(x2, y2, z2, x2out, y2out, z2out);
  else {
    *x2out=x2;;
    *y2out=y2;
    *z2out=z2;
  }
} /* rot_3d */

/*-------------------------------------------------------------
  void cartesian_to_spherical(x, y, z, lon, lat, r)
  may merge with xyz2latlon in the future
  ------------------------------------------------------------*/
void cartesian_to_spherical(double x, double y, double z, double *lon, double *lat, double *r)
{

  *r = sqrt(x*x + y*y + z*z);
  if ( (fabs(x) + fabs(y)) < EPSLN10 )       /* poles */
    *lon = 0.;
  else
    *lon = atan2(y,x);    /* range: [-pi,pi] */


  *lat = acos(z/(*r)) - M_PI/2.;
}/* cartesian_to_spherical */

/*-------------------------------------------------------------------------------
  void spherical_to_cartesian
  convert from spheircal coordinates to xyz coords
  may merge with latlon2xyz in the future
  -----------------------------------------------------------------------------*/
void spherical_to_cartesian(double lon, double lat, double r, double *x, double *y, double *z)
{
  *x = r * cos(lon) * cos(lat);
  *y = r * sin(lon) * cos(lat);

  *z = -r * sin(lat);
} /* spherical_to_cartesian */


/*****************************************************************
   double* excess_of_quad(int ni, int nj, double *vec1, double *vec2,
                          double *vec3, double *vec4 )
*******************************************************************/
double excess_of_quad2(const double *vec1, const double *vec2, const double *vec3, const double *vec4 )
{
  double plane1[3], plane2[3], plane3[3], plane4[3];
  double angle12, angle23, angle34, angle41, excess;
  double ang12, ang23, ang34, ang41;

  plane_normal2(vec1, vec2, plane1);
  plane_normal2(vec2, vec3, plane2);
  plane_normal2(vec3, vec4, plane3);
  plane_normal2(vec4, vec1, plane4);
  angle12 = angle_between_vectors2(plane2,plane1);
  angle23 = angle_between_vectors2(plane3,plane2);
  angle34 = angle_between_vectors2(plane4,plane3);
  angle41 = angle_between_vectors2(plane1,plane4);
  ang12 = M_PI-angle12;
  ang23 = M_PI-angle23;
  ang34 = M_PI-angle34;
  ang41 = M_PI-angle41;
  excess = ang12+ang23+ang34+ang41-2*M_PI;
  /*  excess = 2*M_PI - angle12 - angle23 - angle34 - angle41; */

  return excess;

} /* excess_of_quad */

/*******************************************************************************
double angle_between_vectors(const double *vec1, const double *vec2)
*******************************************************************************/

double angle_between_vectors2(const double *vec1, const double *vec2) {
  int n;
  double vector_prod, nrm1, nrm2;
  double angle;

  vector_prod=vec1[0]*vec2[0] + vec1[1]*vec2[1] + vec1[2]*vec2[2];
  nrm1=pow(vec1[0],2)+pow(vec1[1],2)+pow(vec1[2],2);
  nrm2=pow(vec2[0],2)+pow(vec2[1],2)+pow(vec2[2],2);
  if(nrm1*nrm2>0)
    angle = acos( vector_prod/sqrt(nrm1*nrm2) );
  else
    angle = 0;

  return angle;
} /* angle_between_vectors */


/***********************************************************************
   double* plane_normal(int ni, int nj, double *P1, double *P2)
***********************************************************************/

void plane_normal2(const double *P1, const double *P2, double *plane)
{
  double mag;

  plane[0] = P1[1] * P2[2] - P1[2] * P2[1];
  plane[1] = P1[2] * P2[0] - P1[0] * P2[2];
  plane[2] = P1[0] * P2[1] - P1[1] * P2[0];
  mag=sqrt(pow(plane[0],2) + pow(plane[1],2) + pow(plane[2],2));
  if(mag>0) {
    plane[0]=plane[0]/mag;
    plane[1]=plane[1]/mag;
    plane[2]=plane[2]/mag;
  }

} /* plane_normal */

/******************************************************************

  void calc_rotation_angle()

******************************************************************/

void calc_rotation_angle2(int nxp, double *x, double *y, double *angle_dx, double *angle_dy)
{
  int ip1, im1, jp1, jm1, tp1, tm1, i, j, n, ntiles, nx;
  double lon_scale;
  unsigned int n1, n2, n3;

  nx = nxp-1;
  ntiles = 6;
  for(n=0; n<ntiles; n++) {
    for(j=0; j<nxp; j++) {
      for(i=0; i<nxp; i++) {
        n1 = n*nxp*nxp+j*nxp+i;
        lon_scale = cos(y[n1]*D2R);
        tp1 = n;
        tm1 = n;
        ip1 = i+1;
        im1 = i-1;
        jp1 = j;
        jm1 = j;

        if(ip1 >= nxp) {  /* find the neighbor tile. */
          if(n % 2 == 0) { /* tile 1, 3, 5 */
            tp1 = n+1;
            ip1 = 0;
          }
          else { /* tile 2, 4, 6 */
            tp1 = n+2;
            if(tp1 >= ntiles) tp1 -= ntiles;
            ip1 = nx-j-1;
            jp1 = 0;
          }
        }
        if(im1 < 0) {  /* find the neighbor tile. */
          if(n % 2 == 0) { /* tile 1, 3, 5 */
            tm1 = n-2;
            if(tm1 < 0) tm1 += ntiles;
            jm1 = nx;
            im1 = nx-j;
          }
          else { /* tile 2, 4, 6 */
            tm1 = n-1;
            im1 = nx;
          }
        }
        n1 = n*nxp*nxp+j*nxp+i;
        n2 = tp1*nxp*nxp+jp1*nxp+ip1;
        n3 = tm1*nxp*nxp+jm1*nxp+im1;
        angle_dx[n1] = atan2( y[n2]-y[n3], (x[n2]-x[n3])*lon_scale )*R2D;
        tp1 = n;
        tm1 = n;
        ip1 = i;
        im1 = i;
        jp1 = j+1;
        jm1 = j-1;

        if(jp1 >=nxp) {  /* find the neighbor tile. */
          if(n % 2 == 0) { /* tile 1, 3, 5 */
            tp1 = n+2;
            if(tp1 >= ntiles) tp1 -= ntiles;
            jp1 = nx-i;
            ip1 = 0;
          }
          else { /* tile 2, 4, 6 */
            tp1 = n+1;
            if(tp1 >= ntiles) tp1 -= ntiles;
            jp1 = 0;
          }
        }
        if(jm1 < 0) {  /* find the neighbor tile. */
          if(n % 2 == 0) { /* tile 1, 3, 5 */
            tm1 = n-1;
            if(tm1 < 0) tm1 += ntiles;
            jm1 = nx;
          }
          else { /* tile 2, 4, 6 */
            tm1 = n-2;
            if(tm1 < 0) tm1 += ntiles;
            im1 = nx;
            jm1 = nx-i;
          }
        }

        n1 = n*nxp*nxp+j*nxp+i;
        n2 = tp1*nxp*nxp+jp1*nxp+ip1;
        n3 = tm1*nxp*nxp+jm1*nxp+im1;
        angle_dy[n1] = atan2( y[n2]-y[n3], (x[n2]-x[n3])*lon_scale )*R2D;
      }
    }
  }

} /* calc_rotation_angle2 */


/* This routine calculate center location based on the vertices location */
void cell_center(int ni, int nj, const double *lonc, const double *latc, double *lont, double *latt)
{

  int    nip, njp, i, j, p, p1, p2, p3, p4;
  double *xc, *yc, *zc, *xt, *yt, *zt;
  double dd;

  nip = ni+1;
  njp = nj+1;
  xc = (double *)malloc(nip*njp*sizeof(double));
  yc = (double *)malloc(nip*njp*sizeof(double));
  zc = (double *)malloc(nip*njp*sizeof(double));
  xt = (double *)malloc(ni *nj *sizeof(double));
  yt = (double *)malloc(ni *nj *sizeof(double));
  zt = (double *)malloc(ni *nj *sizeof(double));
  latlon2xyz(nip*njp, lonc, latc, xc, yc, zc);

  for(j=0; j<nj; j++) for(i=0; i<ni; i++) {
      p =  j*ni+i;
      p1 = j*nip+i;
      p2 = j*nip+i+1;
      p3 = (j+1)*nip+i+1;
      p4 = (j+1)*nip+i;
      xt[p] = xc[p1] + xc[p2] + xc[p3] + xc[p4];
      yt[p] = yc[p1] + yc[p2] + yc[p3] + yc[p4];
      zt[p] = zc[p1] + zc[p2] + zc[p3] + zc[p4];

      dd = sqrt(pow(xt[p],2) + pow(yt[p],2) + pow(zt[p],2) );
      xt[p] /= dd;
      yt[p] /= dd;
      zt[p] /= dd;
    }
  xyz2latlon(ni*nj, xt, yt, zt, lont, latt);
  free(zt);
  free(yt);
  free(xt);
  free(zc);
  free(yc);
  free(xc);

} /* cell_center */


/* This routine calculate east location based on the vertices location */
void cell_east(int ni, int nj, const double *lonc, const double *latc, double *lone, double *late)
{

  int    nip, njp, i, j, p, p1, p2;
  double *xc, *yc, *zc, *xe, *ye, *ze;
  double dd;

  nip = ni+1;
  njp = nj+1;
  xc = (double *)malloc(nip*njp*sizeof(double));
  yc = (double *)malloc(nip*njp*sizeof(double));
  zc = (double *)malloc(nip*njp*sizeof(double));
  xe = (double *)malloc(nip*nj *sizeof(double));
  ye = (double *)malloc(nip*nj *sizeof(double));
  ze = (double *)malloc(nip*nj *sizeof(double));
  latlon2xyz(nip*njp, lonc, latc, xc, yc, zc);

  for(j=0; j<nj; j++) for(i=0; i<nip; i++) {
      p =  j*nip+i;
      p1 = j*nip+i;
      p2 = (j+1)*nip+i;
      xe[p] = xc[p1] + xc[p2];
      ye[p] = yc[p1] + yc[p2];
      ze[p] = zc[p1] + zc[p2];

      dd = sqrt(pow(xe[p],2) + pow(ye[p],2) + pow(ze[p],2) );
      xe[p] /= dd;
      ye[p] /= dd;
      ze[p] /= dd;
    }
  xyz2latlon(nip*nj, xe, ye, ze, lone, late);
  free(ze);
  free(ye);
  free(xe);
  free(zc);
  free(yc);
  free(xc);

} /* cell_east */


/* This routine calculate center location based on the vertices location */
void cell_north(int ni, int nj, const double *lonc, const double *latc, double *lonn, double *latn)
{

  int    nip, njp, i, j, p, p1, p2;
  double *xc, *yc, *zc, *xn, *yn, *zn;
  double dd;

  nip = ni+1;
  njp = nj+1;
  xc = (double *)malloc(nip*njp*sizeof(double));
  yc = (double *)malloc(nip*njp*sizeof(double));
  zc = (double *)malloc(nip*njp*sizeof(double));
  xn = (double *)malloc(ni *njp*sizeof(double));
  yn = (double *)malloc(ni *njp*sizeof(double));
  zn = (double *)malloc(ni *njp*sizeof(double));
  latlon2xyz(nip*njp, lonc, latc, xc, yc, zc);

  for(j=0; j<njp; j++) for(i=0; i<ni; i++) {
      p =  j*ni+i;
      p1 = j*nip+i;
      p2 = j*nip+i+1;
      xn[p] = xc[p1] + xc[p2];
      yn[p] = yc[p1] + yc[p2];
      zn[p] = zc[p1] + zc[p2];

      dd = sqrt(pow(xn[p],2) + pow(yn[p],2) + pow(zn[p],2) );
      xn[p] /= dd;
      yn[p] /= dd;
      zn[p] /= dd;
    }
  xyz2latlon(ni*njp, xn, yn, zn, lonn, latn);
  free(zn);
  free(yn);
  free(xn);
  free(zc);
  free(yc);
  free(xc);

} /* cell_north */

/*-------------------------------------------------------------------------------------------
  void spherical_linear_interpolation
  This formula interpolates along the great circle connecting points p1 and p2.
  This formula is taken from http://en.wikipedia.org/wiki/Slerp and is
  attributed to Glenn Davis based on a concept by Ken Shoemake.
  -------------------------------------------------------------------------------------------*/
void spherical_linear_interpolation(double beta, const double *p1, const double *p2, double *pb)
{

  double pm[2];
  double e1[3], e2[3], eb[3];
  double dd, alpha, omega;

  if ( fabs(p1[0] - p2[0]) < EPSLN8 && fabs(p1[1] - p2[1]) < EPSLN8 ) {
    printf("WARNING from create_gnomonic_cubic_grid: spherical_linear_interpolation was passed two colocated points.\n");
    pb[0] = p1[0];
    pb[1] = p1[1];
    return ;
  }

  latlon2xyz(1, p1, p1+1, e1, e1+1, e1+2);
  latlon2xyz(1, p2, p2+1, e2, e2+1, e2+2);

  dd = sqrt( e1[0]*e1[0] + e1[1]*e1[1] + e1[2]*e1[2]);

  e1[0] /= dd;
  e1[1] /= dd;
  e1[2] /= dd;

  dd = sqrt( e2[0]*e2[0] + e2[1]*e2[1] + e2[2]*e2[2]);
  e2[0] /= dd;
  e2[1] /= dd;
  e2[2] /= dd;

  alpha = 1. - beta;

  omega = acos( e1[0]*e2[0] + e1[1]*e2[1] + e1[2]*e2[2] );

  if ( fabs(omega) < EPSLN5 ) {
    printf("spherical_linear_interpolation: omega=%g, p1 = %g,%g, p2 = %g,%g\n",
           omega, p1[0], p1[1], p2[0], p2[1]);
    mpp_error("spherical_linear_interpolation: interpolation not well defined between antipodal points");
  }

  eb[0] = sin( beta*omega )*e2[0] + sin(alpha*omega)*e1[0];
  eb[1] = sin( beta*omega )*e2[1] + sin(alpha*omega)*e1[1];
  eb[2] = sin( beta*omega )*e2[2] + sin(alpha*omega)*e1[2];

  eb[0] /= sin(omega);
  eb[1] /= sin(omega);
  eb[2] /= sin(omega);

  xyz2latlon(1, eb, eb+1, eb+2, pb, pb+1);
}


/***
  Canculate the index into parent.
  Function index_an_gr is introduced to avoid IMAs (an array out of bounds access)
  in some global_refinement situations. In other situations, attempting to
  go outside the parent array should not occur and is considered a fatal error.
***/
int index_an_gr(int jcf, int p_npi, int icf, int max_ni, int max_nj, int is_gr){
  if(jcf > max_nj){
    jcf = max_nj;//then use the uppr (last) row data
    //fprintf(stderr, "[INFO] index_an_gr : jcf=%d max_nj=%d\n",jcf, max_nj);
    if(is_gr == 0){
      mpp_error("make_hgrid in index_ar_gr, jcf > max_nj");
    }
  }
  if(icf > max_ni){
    icf = max_ni;//then use the rightmost column data
    //fprintf(stderr, "[INFO] index_an_gr : icf=%d max_ni=%d\n",icf, max_ni);
    if(is_gr == 0){
      mpp_error("make_hgrid in index_ar_gr, icf > max_ni");
    }
  }
  return ( jcf * p_npi + icf );
}


/* void setup_aligned_nest
   parent_ni    : (input) parent grid size in x-direction.
   parent_nj    : (input) parent grid size in y-direction.
   parent_xc    : (input) parent array in x-direction
   parent_yc    : (input) parent array in y-direction
   halo         : (input) halo size
   refine_ratio : (input) refinement ratio
   istart       : (input) start of nest in x direction
   iend         : (input) end of nest in x direction
   jstart       : (input) start of nest in y direction
   jend         : (input) end of nest in y direction
   xc           : (output) nest array in x-direction
   yc           : (output) nest array in y-direction

*/
void setup_aligned_nest(int parent_ni, int parent_nj, const double *parent_xc, const double *parent_yc,
                        int halo, int refine_ratio, int istart, int iend, int jstart, int jend,
                        double *xc, double *yc, int is_gr)
{
  double q1[2], q2[2], t1[2], t2[2];
  double two_pi;
  int    ni, nj, npi, npj;
  int    parent_npi, i, j, ic, jc, imod, jmod;
  int verbose = 1;

  two_pi = 2.*M_PI;

  /* Check that the grid does not lie outside its parent */
  if( (jstart - halo) < 1 || (istart - halo) < 1 ||
      (jend + halo) > parent_nj || (iend + halo) > parent_ni )
    mpp_error("create_gnomonic_cubic_grid(setup_aligned_nest): nested grid lies outside its parent");

  if (verbose) {
    fprintf(stderr, "[INFO] setup_aligned nest: parent_ni: %d parent_nj: %d refine_ratio: %d parent_xc: %p parent_yc: %p\n",
            parent_ni, parent_nj, refine_ratio, (void *)parent_xc, (void *)parent_yc);

  }

  ni = (iend-istart+1)*refine_ratio;
  nj = (jend-jstart+1)*refine_ratio;
  npi = ni+1;
  npj = nj+1;
  parent_npi = parent_ni+1;

  for(j=0; j<npj; j++) {
    jc = jstart - 1 + j/refine_ratio;
    jmod = j%refine_ratio;
    for(i=0; i<npi; i++) {
      ic = istart - 1 + i/refine_ratio;
      imod = i%refine_ratio;

       if(jmod == 0) {
        int idx = index_an_gr(jc,parent_npi,ic,parent_ni,parent_nj,is_gr);
        int idx_pi = index_an_gr(jc,parent_npi,ic+1,parent_ni,parent_nj,is_gr);
        q1[0] = parent_xc[ idx ];
        q1[1] = parent_yc[ idx ];
        q2[0] = parent_xc[ idx_pi ];
        q2[1] = parent_yc[ idx_pi ];
      }
      else {
        int idx = index_an_gr(jc,parent_npi,ic,parent_ni,parent_nj,is_gr);
        int idx_pi = index_an_gr(jc,parent_npi,ic+1,parent_ni,parent_nj,is_gr);
        int idx_pj = index_an_gr(jc+1,parent_npi,ic,parent_ni,parent_nj,is_gr);
        int idx_pjpi = index_an_gr(jc+1,parent_npi,ic+1,parent_ni,parent_nj,is_gr);
        t1[0] = parent_xc[ idx ];
        t1[1] = parent_yc[ idx ];
        t2[0] = parent_xc[ idx_pj ];
        t2[1] = parent_yc[ idx_pj ];
        spherical_linear_interpolation( (double)jmod/refine_ratio, t1, t2, q1);
        t1[0] = parent_xc[ idx_pi ];
        t1[1] = parent_yc[ idx_pi ];
        t2[0] = parent_xc[ idx_pjpi ];
        t2[1] = parent_yc[ idx_pjpi ];
        spherical_linear_interpolation( (double)jmod/refine_ratio, t1, t2, q2);
      }

      if (imod == 0) {
        xc[j*npi+i] = q1[0];
        yc[j*npi+i] = q1[1];
      }
      else {
        spherical_linear_interpolation( (double)imod/refine_ratio, q1, q2, t1 );
        xc[j*npi+i] = t1[0];
        yc[j*npi+i] = t1[1];
      }

      if( xc[j*npi+i] > two_pi ) xc[j*npi+i] -= two_pi;
      if( xc[j*npi+i] < 0. ) xc[j*npi+i] += two_pi;

      if (verbose && (j==0)) {
        if (i==0) {
          printf("setup_aligned_nest xc[0]: %f yc[0]: %f\n", xc[0], yc[0]);
        } else if (i==1) {
          printf("setup_aligned_nest xc[1]: %f yc[1]: %f\n", xc[1], yc[1]);
        }
      }

    }
  }//end j loop
}
