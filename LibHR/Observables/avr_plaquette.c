/***************************************************************************\
* Copyright (c) 2008, Claudio Pica                                          *   
* All rights reserved.                                                      * 
\***************************************************************************/

/*******************************************************************************
*
* File plaquette.c
*
* Routines for the average plaquette
*
*******************************************************************************/

#include "global.h"
#include "suN.h"
#include "communications.h"
#include "logger.h"
#include "observables.h"
#include "geometry.h"
#include <stdio.h>

double plaq(int ix,int mu,int nu)
{
  int iy,iz;
  double p;
  suNg *v1,*v2,*v3,*v4,w1,w2,w3;

  iy=iup(ix,mu);
  iz=iup(ix,nu);

  v1=pu_gauge(ix,mu);
  v2=pu_gauge(iy,nu);
  v3=pu_gauge(iz,mu);
  v4=pu_gauge(ix,nu);

  _suNg_times_suNg(w1,(*v1),(*v2));
  _suNg_times_suNg(w2,(*v4),(*v3));
  _suNg_times_suNg_dagger(w3,w1,w2);      

  _suNg_trace_re(p,w3);

#ifdef PLAQ_WEIGHTS
  if(plaq_weight==NULL) return p;
  return plaq_weight[ix*16+mu*4+nu]*p;
#else
  return p;
#endif
}


void cplaq(complex *ret,int ix,int mu,int nu)
{
  int iy,iz;;
  suNg *v1,*v2,*v3,*v4,w1,w2,w3;

  iy=iup(ix,mu);
  iz=iup(ix,nu);

  v1=pu_gauge(ix,mu);
  v2=pu_gauge(iy,nu);
  v3=pu_gauge(iz,mu);
  v4=pu_gauge(ix,nu);

  _suNg_times_suNg(w1,(*v1),(*v2));
  _suNg_times_suNg(w2,(*v4),(*v3));
  _suNg_times_suNg_dagger(w3,w1,w2);      
      
  _suNg_trace_re(ret->re,w3);
#ifdef GAUGE_SON
  ret->im=0;
#else
  _suNg_trace_im(ret->im,w3);
#endif

#ifdef PLAQ_WEIGHTS
  if(plaq_weight!=NULL) {
    ret->re *= plaq_weight[ix*16+mu*4+nu];
    ret->im *= plaq_weight[ix*16+mu*4+nu];
  }
#endif

}


double avr_plaquette()
{
  double pa=0.;

  _PIECE_FOR(&glattice,ixp) {
    if(ixp==glattice.inner_master_pieces) {
      _OMP_PRAGMA( master )
      /* wait for gauge field to be transfered */
      complete_gf_sendrecv(u_gauge);
      _OMP_PRAGMA( barrier )
    }
    _SITE_FOR_SUM(&glattice,ixp,ix,pa) {
      pa+=plaq(ix,1,0);
      pa+=plaq(ix,2,0);
      pa+=plaq(ix,2,1);
      pa+=plaq(ix,3,0);
      pa+=plaq(ix,3,1);
      pa+=plaq(ix,3,2);
    }
  }

  global_sum(&pa, 1);

  return pa/(6.*NG)/GLB_VOLUME;

}

void full_plaquette()
{
  complex pa[6];
#ifdef _OPENMP
  double rpa1,rpa2,rpa3,rpa4,rpa5,rpa6,rpa7,rpa8,rpa9,rpa10,rpa11,rpa12; //for OMP reduction
#endif
  
  for(int k=0;k<6;k++) { _complex_0(pa[k]); }

/*  int t=0; */
  
  _PIECE_FOR(&glattice,ixp) {
    if(ixp==glattice.inner_master_pieces) {
      _OMP_PRAGMA( master )
      /* wait for gauge field to be transfered */
      complete_gf_sendrecv(u_gauge);
      _OMP_PRAGMA( barrier )
    }
    _SITE_FOR_SUM(&glattice,ixp,ix,rpa1,rpa2,rpa3,rpa4,rpa5,rpa6,rpa7,rpa8,rpa9,rpa10,rpa11,rpa12) {
      complex tmp;
		  cplaq(&tmp,ix,1,0); _complex_add_assign(pa[0],tmp);
		  cplaq(&tmp,ix,2,0); _complex_add_assign(pa[1],tmp);
		  cplaq(&tmp,ix,2,1); _complex_add_assign(pa[2],tmp);
		  cplaq(&tmp,ix,3,0); _complex_add_assign(pa[3],tmp);
		  cplaq(&tmp,ix,3,1); _complex_add_assign(pa[4],tmp);
		  cplaq(&tmp,ix,3,2); _complex_add_assign(pa[5],tmp);

#ifdef _OPENMP
      rpa1=pa[0].re; rpa2=pa[0].im;
      rpa3=pa[1].re; rpa4=pa[1].im;
      rpa5=pa[2].re; rpa6=pa[2].im;
      rpa7=pa[3].re; rpa8=pa[3].im;
      rpa9=pa[4].re; rpa10=pa[4].im;
      rpa11=pa[5].re; rpa12=pa[5].im;
#endif
      
/*		  
		  if(twbc_plaq[ix*16+2*4+1]==-1 &&
		    twbc_plaq[ix*16+3*4+1]==-1 &&
		    twbc_plaq[ix*16+3*4+2]==-1) {
		  cplaq(&tmp,ix,1,0);
		  lprintf("LOCPL",0,"Plaq( %d , %d , %d ) = ( %f , %f )\n",t,1,0,tmp.re,tmp.im);
		  cplaq(&tmp,ix,2,0);
		  lprintf("LOCPL",0,"Plaq( %d , %d , %d ) = ( %f , %f )\n",t,2,0,tmp.re,tmp.im);
		  cplaq(&tmp,ix,2,1);
		  lprintf("LOCPL",0,"Plaq( %d , %d , %d ) = ( %f , %f )\n",t,2,1,tmp.re,tmp.im);
		  cplaq(&tmp,ix,3,0);
		  lprintf("LOCPL",0,"Plaq( %d , %d , %d ) = ( %f , %f )\n",t,3,0,tmp.re,tmp.im);
		  cplaq(&tmp,ix,3,1);
		  lprintf("LOCPL",0,"Plaq( %d , %d , %d ) = ( %f , %f )\n",t,3,1,tmp.re,tmp.im);
		  cplaq(&tmp,ix,3,2);
		  lprintf("LOCPL",0,"Plaq( %d , %d , %d ) = ( %f , %f )\n",t,3,2,tmp.re,tmp.im);
		  t++;
		    } */
    }
  }

#ifdef _OPENMP
  pa[0].re=rpa1; pa[0].im=rpa2;
  pa[1].re=rpa3; pa[1].im=rpa4;
  pa[2].re=rpa5; pa[2].im=rpa6;
  pa[3].re=rpa7; pa[3].im=rpa8;
  pa[4].re=rpa9; pa[4].im=rpa10;
  pa[5].re=rpa11; pa[5].im=rpa12;
#endif

  
  global_sum((double*)pa,12);
  for(int k=0;k<6;k++) {
    pa[k].re /= GLB_VOLUME*NG;
    pa[k].im /= GLB_VOLUME*NG;
  }

  lprintf("PLAQ",0,"Plaq( %d , %d) = ( %f , %f )\n",1,0,pa[0].re,pa[0].im);
  lprintf("PLAQ",0,"Plaq( %d , %d) = ( %f , %f )\n",2,0,pa[1].re,pa[1].im);
  lprintf("PLAQ",0,"Plaq( %d , %d) = ( %f , %f )\n",2,1,pa[2].re,pa[2].im);
  lprintf("PLAQ",0,"Plaq( %d , %d) = ( %f , %f )\n",3,0,pa[3].re,pa[3].im);
  lprintf("PLAQ",0,"Plaq( %d , %d) = ( %f , %f )\n",3,1,pa[4].re,pa[4].im);
  lprintf("PLAQ",0,"Plaq( %d , %d) = ( %f , %f )\n",3,2,pa[5].re,pa[5].im);

}

double local_plaq(int ix)
{
  double pa;

  pa=plaq(ix,1,0);
  pa+=plaq(ix,2,0);
  pa+=plaq(ix,2,1);
  pa+=plaq(ix,3,0);
  pa+=plaq(ix,3,1);
  pa+=plaq(ix,3,2);

  return pa;

}