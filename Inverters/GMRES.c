/***************************************************************************\
* Copyright (c) 2013, Ari Hietanen, Ulrik Soendergaard, Claudio Pica        *   
* All rights reserved.                                                      * 
\***************************************************************************/

#include "inverters.h"
#include "linear_algebra.h"
#include "complex.h"
#include "memory.h"
#include "update.h"
#include "logger.h"
#include "communications.h"
#include "global.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

static int GMRES_core(short int *valid, MINRES_par *par, int kry_dim , spinor_operator M, spinor_field *in, spinor_field *out, spinor_field *trial){

  int i,j;
  int cgiter = 0;
  double beta;
  spinor_field * w;
  spinor_field * v;
  spinor_field * p;
  complex * h;
  complex c;
  double s;
  double tmp;
  complex * gbar;
  complex * ybar;
  complex c_tmp1,c_tmp2;

  gbar=(complex*)malloc((kry_dim+1)*sizeof(gbar));
  ybar=(complex*)malloc((kry_dim)*sizeof(ybar));

  h=(complex*)malloc(kry_dim*(kry_dim+1)*sizeof(h));
  //h(i,j)=h(i*kry_dim+j)


#ifdef WITH_GPU
  alloc_mem_t=GPU_MEM; /* allocate only on GPU */
#endif
  v = alloc_spinor_field_f(kry_dim+2,in->type);
  w=v+kry_dim;
  p=v+kry_dim+1;
  alloc_mem_t=std_mem_t; /* set the allocation memory type back */

  spinor_field_copy_f(&v[0], in);  // p2 now has the rhs-spinorfield "b"
  if(trial!=NULL) {
    M.dbl(p,trial);
    ++cgiter;
    spinor_field_sub_assign_f(&v[0],p);
    
    if(out!=trial){
      spinor_field_copy_f(out,trial);
    }
    
  } else {
    spinor_field_zero_f(out);
  }

  beta=sqrt(spinor_field_sqnorm_f(&v[0]));
  lprintf("INVERTER",100,"GMRES Initial norm: %e\n",beta);

  spinor_field_mul_f(&v[0],1./beta,&v[0]);
  gbar[0].re=beta;
  gbar[0].im=0;

  for (j=0;j<kry_dim;++j){
    
    ++cgiter;
    M.dbl(w,&v[j]);    
    lprintf("INVERTER",500,"GMRES Applied Dirac operator:\n");

    for (i=0;i<j+1;++i){	// Gram-Schmidth orthogonalization
      h[i*kry_dim+j]=spinor_field_prod_f(w,&v[i]);
      lprintf("INVERTER",500,"GMRES Gram-Schmidt 1!:\n");      
      _complex_minus(c_tmp1,h[i*kry_dim+j]); 
      spinor_field_mulc_add_assign_f(w,c_tmp1,&v[i]);
      lprintf("INVERTER",500,"GMRES Gram-Schmidt 2!:\n");      
    }
    lprintf("INVERTER",500,"GMRES Gram-Schmidt orthogonalization done!:\n");

    h[(j+1)*kry_dim+j].re=sqrt(spinor_field_sqnorm_f(w));
    h[(j+1)*kry_dim+j].im=0;
    
    if (h[(j+1)*kry_dim+j].re < par->err2){
      kry_dim=j;
      break;
    }
    
    if (j<kry_dim-1){
      spinor_field_mul_f(&v[j+1],1./h[(j+1)*kry_dim+j].re,w);
    }
    
    gbar[j+1].re = gbar[j+1].im = 0.;
  }

  lprintf("INVERTER",100,"GMRES loopek over kyrlov_dim:%d!\n",kry_dim);

  // Now doing the Givens rotations
  for (i=0;i<kry_dim-1;++i){
    tmp=sqrt(_complex_prod_re(h[i*kry_dim+i], h[i*kry_dim+i])+h[i*(kry_dim+1)+i].re*h[i*(kry_dim+1)+i].re);
    s=h[i*(kry_dim+1)+i].re / tmp;
    _complex_mulr(c,1./tmp,h[i*kry_dim+i]);
	
    // Rotate gbar
    c_tmp1=gbar[i];
    _complex_mul_star(gbar[i],c_tmp1,c);

    c_tmp2=gbar[i+1];
    _complex_mulr(gbar[i+1],-s,c_tmp1);
    _complex_mul_assign(gbar[i+1],c,c_tmp2);
	
	
    // Rotate h
    for (j=i;j<kry_dim+1;++j){
      c_tmp1=h[i*kry_dim+j];
      _complex_mul_star(h[i*kry_dim+j],c_tmp1,c);
      _complex_mulr_assign(h[i*kry_dim+j],s,h[(i+1)*kry_dim+j]);	
      c_tmp2=h[(1+i)*kry_dim+j];
      _complex_mulr(h[(i+1)*kry_dim+j],-s,c_tmp1);
      _complex_mul_assign(h[(i+1)*kry_dim+j],c,c_tmp2);
    }
  }


  // h is now upper triangular... 
  // now the vector that minimizes |\beta e_1 - h y| is found by y = h^-1 g
  for (i=kry_dim-1;i>=0;--i){
    ybar[i]=gbar[i];
    for (j=kry_dim-1;j>i;--j){
      _complex_mul_sub_assign(ybar[i],h[i*kry_dim+j],ybar[j]);
    }
    _complex_mulr(ybar[i],1./h[i*kry_dim+i].re,ybar[i]);
  }

  //The solutions is (out_m = out_0 + V_m y_m)
  //  for (i=0;i<kry_dim;++i){
  spinor_field_mulc_add_assign_f(out,ybar[kry_dim-1],&v[kry_dim-1]);
  // }
  
  *valid = (gbar[kry_dim].re < par->err2 );
    
  lprintf("INVERTER",30,"GMRES Error after GMRES_core: %e\n",gbar[kry_dim]);
  
  /* free memory */
  free_spinor_field_f(v);
  
  /* return number of cg iter */
  return cgiter;
}


int GMRES(MINRES_par *par, spinor_operator M, spinor_field *in, spinor_field *out, spinor_field *trial){
  int iter, rep=0;
  short int valid;
  int kry_dim=10;
  lprintf("INVERTER",100,"GMRES starting inverter!\n",rep);  
  iter = GMRES_core(&valid,par,kry_dim,M,in,out,trial);
  while (!valid && (par->max_iter==0 || iter < par->max_iter)){
    iter += GMRES_core(&valid,par,kry_dim,M,in,out,out);
    if ((++rep %10 == 0 )){
      lprintf("INVERTER",-10,"GMRES recursion = %d (precision too high?)\n",rep);
    }
  }
  lprintf("INVERTER",10,"GMMINRES: MVM = %d\n",iter);
  return iter;
}
