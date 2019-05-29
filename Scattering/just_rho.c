// This code will contain all the contractions necessary for rho to pi pi scattering
#define MAIN_PROGRAM

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "global.h"
#include "io.h"
#include "random.h"
#include "error.h"
#include "geometry.h"
#include "memory.h"
#include "statistics.h"
#include "update.h"
#include "scattering.h"
#include "observables.h"
#include "suN.h"
#include "suN_types.h"
#include "dirac.h"
#include "linear_algebra.h"
#include "inverters.h"
#include "representation.h"
#include "utils.h"
#include "logger.h"
#include "communications.h"
#include "gamma_spinor.h"
#include "spin_matrix.h"
#include "clover_tools.h"

#include "cinfo.c"
#include "IOroutines.c"

#define PI 3.141592653589793238462643383279502884197

static void flip_T_bc(int tau){
  int index;
  int ix,iy,iz;
  suNf *u;
  tau-=1;
  if (tau<0) tau+= GLB_T;
  lprintf("meson_measurements",15,"Flipping the boundary at global time slice %d\n",tau);
  fflush(stdout);
  if((zerocoord[0]-1<=tau && zerocoord[0]+T>tau) || (zerocoord[0]==0 && tau==GLB_T-1)) { 
    for (ix=0;ix<X_EXT;++ix) for (iy=0;iy<Y_EXT;++iy) for (iz=0;iz<Z_EXT;++iz){
	  if ((tau==zerocoord[0]-1) || (zerocoord[0]==0 && tau==GLB_T-1)){
	    index=ipt_ext(0,ix,iy,iz);
	  }
	  else{
	    index=ipt_ext(T_BORDER+tau-zerocoord[0],ix,iy,iz);
	  }
	  if(index!=-1) {
	    u=pu_gauge_f(index,0);
	    _suNf_minus(*u,*u);
	  }
	}
  }
  lprintf("meson_measurements",50,"Flipping DONE!\n");
}
// Function for initiating meson observable (used to store the correlation function)
void init_mo(meson_observable* mo, char* name, int size)
{
  int i;
  //ind1 and ind2 don't do anything for the moment
  mo->ind1 = _g5;
  mo->ind2 = _g5;
  strcpy(mo->channel_name,name);
  strcpy(mo->channel_type, "Pi Pi scattering");
  mo->sign=1.0;
  mo->corr_size=size;
  mo->corr_re = (double * ) malloc(size * sizeof(double));
  mo->corr_im = (double * ) malloc(size * sizeof(double));
  if (mo->corr_re == NULL || mo->corr_im == NULL)
  {
    fprintf(stderr, "malloc failed in init_mo \n");
    return;
  }
  mo->next=NULL;
  for (i=0; i<size; ++i)
  {
    mo->corr_re[i]=0.0;
    mo->corr_im[i]=0.0;
  }
}

void reset_mo(meson_observable* mo)
{
  int i;
  for (i=0; i< mo->corr_size; ++i)
  {
    mo->corr_re[i]=0.0;
    mo->corr_im[i]=0.0;
  }
}

static void do_global_sum(meson_observable* mo, double norm){
  meson_observable* motmp=mo;
  int i;
  while (motmp!=NULL){
      global_sum(motmp->corr_re,motmp->corr_size);
      global_sum(motmp->corr_im,motmp->corr_size);
      for(i=0; i<motmp->corr_size; i++){
	motmp->corr_re[i] *= norm;
	motmp->corr_im[i] *= norm;
      }
    motmp=motmp->next;
  }
}

void free_mo(meson_observable* mo)
{
  free(mo->corr_re);
  free(mo->corr_im);
  free(mo);
}

#define BASENAME(filename) (strrchr((filename),'/') ? strrchr((filename),'/')+1 : filename )

#define corr_ind(px,py,pz,n_mom,tc,nm,cm) ((px)*(n_mom)*(n_mom)*GLB_T*(nm)+(py)*(n_mom)*GLB_T*(nm)+(pz)*GLB_T*(nm)+ ((cm)*GLB_T) +(tc))
inline void io2pt(meson_observable* mo_plus, meson_observable* mo_minus, int t0, int pmax, int sourceno, char* path, char* name)
{
	FILE* file;
	char outfile[256] = {};
	int px,py,pz,t;
	if(PID==0){
		sprintf(outfile,"%s/%s_src_%d_%s", path, name, sourceno, BASENAME(cnfg_filename) );
		file=fopen(outfile,"w+");
		//Factor of 2 to correct for the noise source normalisation
		for(px=0;px<pmax;++px) for(py=0;py<pmax;++py) for(pz=0;pz<pmax;++pz){
            for(t=0;t<GLB_T;++t){ 
            //for(t=0;t<GLB_T-t0;++t){ 
//                fprintf(file,"%i %i %i %i %3.10e %3.10e \n", px, py, pz, t,2*(mo_plus->corr_re[corr_ind(px,py,pz,pmax,t,1,0)]), 2*(mo_plus->corr_im[corr_ind(px,py,pz,pmax,t,1,0)]));
//            }
            //for(t=GLB_T-t0;t<GLB_T;++t){ 
                fprintf(file,"%i %i %i %i %3.10e %3.10e \n", px, py, pz, t,2*(mo_minus->corr_re[corr_ind(px,py,pz,pmax,t,1,0)]), 2*(mo_minus->corr_im[corr_ind(px,py,pz,pmax,t,1,0)]));
            }
        }
		fclose(file);
	}
	return;
}

#define INDEX(px,py,pz,n_mom,tc) ((px + n_mom)*(2*n_mom+1)*(2*n_mom+1)*(GLB_T)+(py + n_mom)*(2*n_mom+1)*(GLB_T)+(pz + n_mom)*(GLB_T)+ (tc))

int main(int argc,char *argv[])
{
  int src,t;
  int px,py,pz, px2, py2, pz2, px3, py3, pz3;
  int tau=0;
  filename_t fpars;
  int nm;
  char tmp[256], *cptr;
  int i,k;
  double m[256];
  FILE* list;

  //Copy I/O from another file
  read_cmdline(argc, argv);
  setup_process(&argc,&argv);

  read_input(glb_var.read,input_filename);
  setup_replicas();

  /* logger setup */
  /* disable logger for MPI processes != 0 */
  logger_setlevel(0,10);
  if (PID!=0) { logger_disable(); }
  if (PID==0) { 
    sprintf(tmp,">%s",output_filename); logger_stdout(tmp);
    sprintf(tmp,"err_%d",PID); freopen(tmp,"w",stderr);
  }

  lprintf("MAIN",0,"Compiled with macros: %s\n",MACROS); 
  lprintf("MAIN",0,"PId =  %d [world_size: %d]\n\n",PID,WORLD_SIZE); 
  lprintf("MAIN",0,"input file [%s]\n",input_filename); 
  lprintf("MAIN",0,"output file [%s]\n",output_filename); 
  if (list_filename!=NULL) lprintf("MAIN",0,"list file [%s]\n",list_filename); 
  else lprintf("MAIN",0,"cnfg file [%s]\n",cnfg_filename); 

  list=NULL;
  if(strcmp(list_filename,"")!=0) {
    error((list=fopen(list_filename,"r"))==NULL,1,"main [mk_mesons.c]" ,
	"Failed to open list file\n");
  }

  /* read & broadcast parameters */
  parse_cnfg_filename(cnfg_filename,&fpars);

  read_input(mes_var.read,input_filename);
  char *path=mes_var.outdir;
  printf("The momenta are (%s) and (%s) \n", mes_var.p1, mes_var.p2);
  sscanf(mes_var.p1, "(%d,%d,%d)" ,&px,&py,&pz);
  sscanf(mes_var.p2, "(%d,%d,%d)" ,&px2,&py2,&pz2);
  sscanf(mes_var.p3, "(%d,%d,%d)" ,&px3,&py3,&pz3);
  printf("The momenta are (%d %d %d), (%d %d %d) and (%d %d %d) \n", px, py, pz, px2, py2, pz2, px3, py3, pz3);
  int numsources = mes_var.nhits;
  tau = mes_var.tsrc;
  printf("Inverting at time slice %d \n", tau);
  GLB_T=fpars.t; GLB_X=fpars.x; GLB_Y=fpars.y; GLB_Z=fpars.z;

  /* setup lattice geometry */
  if (geometry_init() == 1) { finalize_process(); return 0; }
  /* setup random numbers */
  read_input(rlx_var.read,input_filename);
  //slower(rlx_var.rlxd_start); //convert start variable to lowercase
  if(strcmp(rlx_var.rlxd_start,"continue")==0 && rlx_var.rlxd_state[0]!='\0') {
    /*load saved state*/
    lprintf("MAIN",0,"Loading rlxd state from file [%s]\n",rlx_var.rlxd_state);
    read_ranlxd_state(rlx_var.rlxd_state);
  } else {
    lprintf("MAIN",0,"RLXD [%d,%d]\n",rlx_var.rlxd_level,rlx_var.rlxd_seed+MPI_PID);
    rlxd_init(rlx_var.rlxd_level,rlx_var.rlxd_seed+MPI_PID); /* use unique MPI_PID to shift seeds */
  }

  lprintf("MAIN",0,"Gauge group: SU(%d)\n",NG);
  lprintf("MAIN",0,"Fermion representation: " REPR_NAME " [dim=%d]\n",NF);

  nm=0;
  if(fpars.type==DYNAMICAL_CNFG) {
    nm=1;
    m[0] = fpars.m;
  } else if(fpars.type==QUENCHED_CNFG) {
    strcpy(tmp,mes_var.mstring);
    cptr = strtok(tmp, ";");
    nm=0;
    while(cptr != NULL) {
      m[nm]=atof(cptr);
      nm++;
      cptr = strtok(NULL, ";");
      printf(" %3.3e \n",m[nm]);
    }            
  }



  /* setup communication geometry */
  if (geometry_init() == 1) {
    finalize_process();
    return 0;
  }

  /* setup lattice geometry */
  geometry_mpi_eo();
  /* test_geometry_mpi_eo(); */

  init_BCs(NULL);



  /* alloc global gauge fields */
  u_gauge=alloc_gfield(&glattice);
#ifdef ALLOCATE_REPR_GAUGE_FIELD
  u_gauge_f=alloc_gfield_f(&glattice);
#endif
#ifdef WITH_CLOVER
  clover_init(mes_var.csw);
#endif
#ifdef WITH_SMEARING
	init_smearing(mes_var.rho_s, mes_var.rho_t);
#endif

  lprintf("MAIN",0,"Inverter precision = %e\n",mes_var.precision);
  for(k=0;k<nm;k++)
  {
    lprintf("MAIN",0,"Mass[%d] = %f\n",k,m[k]);
    lprintf("CORR",0,"Mass[%d] = %f\n",k,m[k]);
  }

  read_gauge_field(cnfg_filename);
  represent_gauge_field();
  //End of the I/O block

  init_propagator_eo(nm,m,mes_var.precision);

#define OBSERVABLE_LIST X(pi2p) X(pi_p1) X(pi_p2) X(pi_p3) X(twopt_nomom_g1) X(twopt_nomom_g2) X(twopt_nomom_g3) X(twopt_g1) X(twopt_g2) X(twopt_g3) X(twoptp2_g1) X(twoptp2_g2) X(twoptp2_g3) X(twoptp2_g1g2) X(twoptp2_g2g1) X(twoptp3_g1) X(twoptp3_g2) X(twoptp3_g3) X(twoptp3_g1g2) X(twoptp3_g1g3) X(twoptp3_g2g1) X(twoptp3_g2g3) X(twoptp3_g3g1) X(twoptp3_g3g2) 

#define X(NAME) meson_observable* mo_##NAME = malloc(sizeof(meson_observable)); init_mo(mo_##NAME,#NAME,27*GLB_T);\
  meson_observable* mo_##NAME##_A = malloc(sizeof(meson_observable)); init_mo(mo_##NAME##_A,#NAME,27*GLB_T);
OBSERVABLE_LIST
#undef X

mo_twopt_nomom_g1->ind1=_g1;
mo_twopt_nomom_g1->ind2=_g1;
mo_twopt_g1->ind1=_g1;
mo_twopt_g1->ind2=_g1;
mo_twoptp2_g1->ind1=_g1;
mo_twoptp2_g1->ind2=_g1;
mo_twoptp3_g1->ind1=_g1;
mo_twoptp3_g1->ind2=_g1;

mo_twopt_nomom_g2->ind1=_g2;
mo_twopt_nomom_g2->ind2=_g2;
mo_twopt_g2->ind1=_g2;
mo_twopt_g2->ind2=_g2;
mo_twoptp2_g2->ind1=_g2;
mo_twoptp2_g2->ind2=_g2;
mo_twoptp3_g2->ind1=_g2;
mo_twoptp3_g2->ind2=_g2;

mo_twopt_nomom_g3->ind1=_g3;
mo_twopt_nomom_g3->ind2=_g3;
mo_twopt_g3->ind1=_g3;
mo_twopt_g3->ind2=_g3;
mo_twoptp2_g3->ind1=_g3;
mo_twoptp2_g3->ind2=_g3;
mo_twoptp3_g3->ind1=_g3;
mo_twoptp3_g3->ind2=_g3;

mo_twoptp2_g1g2->ind1=_g1;
mo_twoptp2_g1g2->ind2=_g2;
mo_twoptp2_g2g1->ind1=_g2;
mo_twoptp2_g2g1->ind2=_g1;

mo_twoptp3_g1g2->ind1=_g1;
mo_twoptp3_g1g2->ind2=_g2;
mo_twoptp3_g2g1->ind1=_g2;
mo_twoptp3_g2g1->ind2=_g1;
mo_twoptp3_g1g3->ind1=_g1;
mo_twoptp3_g1g3->ind2=_g3;
mo_twoptp3_g3g1->ind1=_g3;
mo_twoptp3_g3g1->ind2=_g1;
mo_twoptp3_g2g3->ind1=_g2;
mo_twoptp3_g2g3->ind2=_g3;
mo_twoptp3_g3g2->ind1=_g3;
mo_twoptp3_g3g2->ind2=_g2;


mo_twopt_nomom_g1_A->ind1=_g1;
mo_twopt_nomom_g1_A->ind2=_g1;
mo_twopt_g1_A->ind1=_g1;
mo_twopt_g1_A->ind2=_g1;
mo_twoptp2_g1_A->ind1=_g1;
mo_twoptp2_g1_A->ind2=_g1;
mo_twoptp3_g1_A->ind1=_g1;
mo_twoptp3_g1_A->ind2=_g1;

mo_twopt_nomom_g2_A->ind1=_g2;
mo_twopt_nomom_g2_A->ind2=_g2;
mo_twopt_g2_A->ind1=_g2;
mo_twopt_g2_A->ind2=_g2;
mo_twoptp2_g2_A->ind1=_g2;
mo_twoptp2_g2_A->ind2=_g2;
mo_twoptp3_g2_A->ind1=_g2;
mo_twoptp3_g2_A->ind2=_g2;

mo_twopt_nomom_g3_A->ind1=_g3;
mo_twopt_nomom_g3_A->ind2=_g3;
mo_twopt_g3_A->ind1=_g3;
mo_twopt_g3_A->ind2=_g3;
mo_twoptp2_g3_A->ind1=_g3;
mo_twoptp2_g3_A->ind2=_g3;
mo_twoptp3_g3_A->ind1=_g3;
mo_twoptp3_g3_A->ind2=_g3;

mo_twoptp2_g1g2_A->ind1=_g1;
mo_twoptp2_g1g2_A->ind2=_g2;
mo_twoptp2_g2g1_A->ind1=_g2;
mo_twoptp2_g2g1_A->ind2=_g1;

mo_twoptp3_g1g2_A->ind1=_g1;
mo_twoptp3_g1g2_A->ind2=_g2;
mo_twoptp3_g2g1_A->ind1=_g2;
mo_twoptp3_g2g1_A->ind2=_g1;
mo_twoptp3_g1g3_A->ind1=_g1;
mo_twoptp3_g1g3_A->ind2=_g3;
mo_twoptp3_g3g1_A->ind1=_g3;
mo_twoptp3_g3g1_A->ind2=_g1;
mo_twoptp3_g2g3_A->ind1=_g2;
mo_twoptp3_g2g3_A->ind2=_g3;
mo_twoptp3_g3g2_A->ind1=_g3;
mo_twoptp3_g3g2_A->ind2=_g2;
#define SOURCE_LIST_Q X(0) X(0_eta) X(p) X(mp) X(p2) X(mp2) X(p3) X(mp3) 
#define X(NAME) lprintf("DEBUG",0,"Creating source source_%s \n", #NAME);spinor_field* source_##NAME = alloc_spinor_field_f(4*NF,&glattice); for(i=0;i<4*NF;++i) {spinor_field_zero_f(&source_##NAME[i]);}
    SOURCE_LIST_Q
    spinor_field* source_0_0 = alloc_spinor_field_f(4*NF,&glattice); spinor_field_zero_f(source_0_0);
#undef X
#define X(NAME) lprintf("DEBUG",0,"Creating propagator Q_%s \n", #NAME);\
    spinor_field* Q_##NAME = alloc_spinor_field_f(4*NF,&glattice);\
    spinor_field* Q_##NAME##_A = alloc_spinor_field_f(4*NF,&glattice);
    SOURCE_LIST_Q
#undef X

while(1){
    if(list!=NULL)
      if(fscanf(list,"%s",cnfg_filename)==0 || feof(list)) break;

    lprintf("MAIN",0,"Configuration from %s\n", cnfg_filename);
    read_gauge_field(cnfg_filename);
    represent_gauge_field();

    for (src=0;src<numsources;++src)
    {
	    // Clear all meson observables
#define X(NAME) reset_mo(mo_##NAME);
	    OBSERVABLE_LIST
#undef X
    lprintf("MAIN",0,"Cleared mo", cnfg_filename);


    	// Sources for non-sequential props
  	create_diluted_source_equal_atau(source_0, tau);
  	create_diluted_source_equal_atau(source_0_eta, tau);
    add_momentum(source_p, source_0, px, py, pz);
    add_momentum(source_mp, source_0, -px, -py, -pz);
    add_momentum(source_p2, source_0, px2, py2, pz2);
    add_momentum(source_mp2, source_0, -px2, -py2, -pz2);
    add_momentum(source_p3, source_0, px3, py3, pz3);
    add_momentum(source_mp3, source_0, -px3, -py3, -pz3);
	lprintf("DEBUG",0,"Created sources \n");
	// Non-sequential prop inversions
    int l;
#define X(NAME) lprintf("DEBUG",0,"Calculating propagator Q_%s \n", #NAME); calc_propagator(Q_##NAME, source_##NAME,4);\
    calc_propagator(Q_##NAME, source_##NAME,4);\
    flip_T_bc(tau);\
    calc_propagator(Q_##NAME##_A, source_##NAME,4);\
    flip_T_bc(tau);\
    for(l=0;l<4;++l){\
        spinor_field_add_assign_f(&Q_##NAME[l],&Q_##NAME##_A[l]);\
        spinor_field_mul_f(&Q_##NAME[l],0.5,&Q_##NAME[l]);\
        spinor_field_mul_f(&Q_##NAME##_A[l],-1.,&Q_##NAME##_A[l]);\
        spinor_field_add_assign_f(&Q_##NAME##_A[l],&Q_##NAME[l]);\
    }
	SOURCE_LIST_Q
#undef X
	lprintf("DEBUG",0,"Created propagators \n");

	//2-point pi -> pi
    lprintf("DEBUG",0,"Calculating contractions \n");

	measure_mesons_core(Q_0, Q_0, source_0, mo_pi2p, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_pi2p,1.0);
	measure_mesons_core(Q_0, Q_p, source_0, mo_pi_p1, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_pi_p1,1.0);
	measure_mesons_core(Q_0, Q_p2, source_0, mo_pi_p2, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_pi_p2,1.0);
	measure_mesons_core(Q_0, Q_p3, source_0, mo_pi_p3, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_pi_p3,1.0);


	measure_mesons_core(Q_0_A, Q_0_A, source_0, mo_pi2p_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_pi2p_A,1.0);
	measure_mesons_core(Q_0_A, Q_p_A, source_0, mo_pi_p1_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_pi_p1_A,1.0);
	measure_mesons_core(Q_0_A, Q_p2_A, source_0, mo_pi_p2_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_pi_p2_A,1.0);
	measure_mesons_core(Q_0_A, Q_p3_A, source_0, mo_pi_p3_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_pi_p3_A,1.0);
	lprintf("DEBUG",0,"pi 2-point function diagrams done \n");

	// 2-point rho->rho
	measure_mesons_core(Q_0, Q_0, source_0, mo_twopt_nomom_g1, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twopt_nomom_g1,1.0);
	measure_mesons_core(Q_0, Q_p, source_0, mo_twopt_g1, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twopt_g1,1.0);
	measure_mesons_core(Q_0, Q_p2, source_0, mo_twoptp2_g1, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp2_g1,1.0);
	measure_mesons_core(Q_0, Q_p3, source_0, mo_twoptp3_g1, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g1,1.0);

	measure_mesons_core(Q_0, Q_0, source_0, mo_twopt_nomom_g2, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twopt_nomom_g2,1.0);
	measure_mesons_core(Q_0, Q_p, source_0, mo_twopt_g2, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twopt_g2,1.0);
	measure_mesons_core(Q_0, Q_p2, source_0, mo_twoptp2_g2, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp2_g2,1.0);
	measure_mesons_core(Q_0, Q_p3, source_0, mo_twoptp3_g2, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g2,1.0);

	measure_mesons_core(Q_0, Q_0, source_0, mo_twopt_nomom_g3, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twopt_nomom_g3,1.0);
	measure_mesons_core(Q_0, Q_p, source_0, mo_twopt_g3, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twopt_g3,1.0);
	measure_mesons_core(Q_0, Q_p2, source_0, mo_twoptp2_g3, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp2_g3,1.0);
	measure_mesons_core(Q_0, Q_p2, source_0, mo_twoptp2_g1g2, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp2_g1g2,1.0);
	measure_mesons_core(Q_0, Q_p2, source_0, mo_twoptp2_g2g1, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp2_g2g1,1.0);
	measure_mesons_core(Q_0, Q_p3, source_0, mo_twoptp3_g3, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g3,1.0);
	measure_mesons_core(Q_0, Q_p3, source_0, mo_twoptp3_g1g2, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g1g2,1.0);
	measure_mesons_core(Q_0, Q_p3, source_0, mo_twoptp3_g2g1, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g2g1,1.0);
	measure_mesons_core(Q_0, Q_p3, source_0, mo_twoptp3_g1g3, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g1g3,1.0);
	measure_mesons_core(Q_0, Q_p3, source_0, mo_twoptp3_g3g1, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g3g1,1.0);
	measure_mesons_core(Q_0, Q_p3, source_0, mo_twoptp3_g2g3, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g2g3,1.0);
	measure_mesons_core(Q_0, Q_p3, source_0, mo_twoptp3_g3g2, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g3g2,1.0);


	measure_mesons_core(Q_0_A, Q_0_A, source_0, mo_twopt_nomom_g1_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twopt_nomom_g1_A,1.0);
	measure_mesons_core(Q_0_A, Q_p_A, source_0, mo_twopt_g1_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twopt_g1_A,1.0);
	measure_mesons_core(Q_0_A, Q_p2_A, source_0, mo_twoptp2_g1_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp2_g1_A,1.0);
	measure_mesons_core(Q_0_A, Q_p3_A, source_0, mo_twoptp3_g1_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g1_A,1.0);

	measure_mesons_core(Q_0_A, Q_0_A, source_0, mo_twopt_nomom_g2_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twopt_nomom_g2_A,1.0);
	measure_mesons_core(Q_0_A, Q_p_A, source_0, mo_twopt_g2_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twopt_g2_A,1.0);
	measure_mesons_core(Q_0_A, Q_p2_A, source_0, mo_twoptp2_g2_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp2_g2_A,1.0);
	measure_mesons_core(Q_0_A, Q_p3_A, source_0, mo_twoptp3_g2_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g2_A,1.0);

	measure_mesons_core(Q_0_A, Q_0_A, source_0, mo_twopt_nomom_g3_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twopt_nomom_g3_A,1.0);
	measure_mesons_core(Q_0_A, Q_p_A, source_0, mo_twopt_g3_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twopt_g3_A,1.0);
	measure_mesons_core(Q_0_A, Q_p2_A, source_0, mo_twoptp2_g3_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp2_g3_A,1.0);
	measure_mesons_core(Q_0_A, Q_p2_A, source_0, mo_twoptp2_g1g2_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp2_g1g2_A,1.0);
	measure_mesons_core(Q_0_A, Q_p2_A, source_0, mo_twoptp2_g2g1_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp2_g2g1_A,1.0);
	measure_mesons_core(Q_0_A, Q_p3_A, source_0, mo_twoptp3_g3_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g3_A,1.0);
	measure_mesons_core(Q_0_A, Q_p3_A, source_0, mo_twoptp3_g1g2_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g1g2_A,1.0);
	measure_mesons_core(Q_0_A, Q_p3_A, source_0, mo_twoptp3_g2g1_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g2g1_A,1.0);
	measure_mesons_core(Q_0_A, Q_p3_A, source_0, mo_twoptp3_g1g3_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g1g3_A,1.0);
	measure_mesons_core(Q_0_A, Q_p3_A, source_0, mo_twoptp3_g3g1_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g3g1_A,1.0);
	measure_mesons_core(Q_0_A, Q_p3_A, source_0, mo_twoptp3_g2g3_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g2g3_A,1.0);
	measure_mesons_core(Q_0_A, Q_p3_A, source_0, mo_twoptp3_g3g2_A, 1, tau, 2, 0, GLB_T);
	do_global_sum(mo_twoptp3_g3g2_A,1.0);
	lprintf("DEBUG",0,"Rho 2-point functions done \n");
	

    char buf[20];

	//File IO
	sprintf(buf, "pi_tau_%d",tau);
    io2pt(mo_pi2p,mo_pi2p_A,tau, 2, src, path, buf);
    sprintf(buf, "pi_p1_tau_%d", tau);
	io2pt(mo_pi_p1,mo_pi_p1_A,tau, 2, src, path, buf);
    sprintf(buf, "pi_p2_tau_%d", tau);
	io2pt(mo_pi_p2,mo_pi_p2_A,tau, 2, src, path, buf);
    sprintf(buf, "pi_p3_tau_%d", tau);
	io2pt(mo_pi_p3,mo_pi_p3_A,tau, 2, src, path, buf);

    sprintf(buf, "rho_p0_g1_tau_%d", tau);
	io2pt(mo_twopt_nomom_g1,mo_twopt_nomom_g1_A,tau, 2, src, path, buf);
    sprintf(buf, "rho_g1_tau_%d", tau);
	io2pt(mo_twopt_g1,mo_twopt_g1_A,tau, 2, src, path, buf);
    sprintf(buf, "rhop2_g1_tau_%d", tau);
	io2pt(mo_twoptp2_g1,mo_twoptp2_g1_A,tau, 2, src, path, buf);
    sprintf(buf, "rhop3_g1_tau_%d", tau);
	io2pt(mo_twoptp3_g1,mo_twoptp3_g1_A,tau, 2, src, path, buf);

    sprintf(buf, "rho_p0_g2_tau_%d", tau);
	io2pt(mo_twopt_nomom_g2,mo_twopt_nomom_g2_A,tau, 2, src, path, buf);
    sprintf(buf, "rho_g2_tau_%d", tau);
	io2pt(mo_twopt_g2,mo_twopt_g2_A,tau, 2, src, path, buf);
    sprintf(buf, "rhop2_g2_tau_%d", tau);
	io2pt(mo_twoptp2_g2,mo_twoptp2_g2_A,tau, 2, src, path, buf);
    sprintf(buf, "rhop3_g2_tau_%d", tau);
	io2pt(mo_twoptp3_g2,mo_twoptp3_g2_A,tau, 2, src, path, buf);

    sprintf(buf, "rho_p0_g3_tau_%d", tau);
	io2pt(mo_twopt_nomom_g3,mo_twopt_nomom_g3_A,tau, 2, src, path, buf);
    sprintf(buf, "rho_g3_tau_%d", tau);
	io2pt(mo_twopt_g3,mo_twopt_g3_A,tau, 2, src, path, buf);
    sprintf(buf, "rhop2_g3_tau_%d", tau);
	io2pt(mo_twoptp2_g3,mo_twoptp2_g3_A,tau, 2, src, path, buf);
    sprintf(buf, "rhop3_g3_tau_%d", tau);
	io2pt(mo_twoptp3_g3,mo_twoptp3_g3_A,tau, 2, src, path, buf);

    sprintf(buf, "rhop2_g1g2_tau_%d", tau);
	io2pt(mo_twoptp2_g1g2,mo_twoptp2_g1g2_A,tau, 2, src, path, buf);
    sprintf(buf, "rhop2_g2g1_tau_%d", tau);
	io2pt(mo_twoptp2_g2g1,mo_twoptp2_g2g1_A,tau, 2, src, path, buf);

    sprintf(buf, "rhop3_g1g2_tau_%d", tau);
	io2pt(mo_twoptp3_g1g2,mo_twoptp3_g1g2_A,tau, 2, src, path, buf);
    sprintf(buf, "rhop3_g2g1_tau_%d", tau);
	io2pt(mo_twoptp3_g2g1,mo_twoptp3_g2g1_A,tau, 2, src, path, buf);
    sprintf(buf, "rhop3_g1g3_tau_%d", tau);
	io2pt(mo_twoptp3_g1g3,mo_twoptp3_g1g3_A,tau, 2, src, path, buf);
    sprintf(buf, "rhop3_g3g1_tau_%d", tau);
	io2pt(mo_twoptp3_g3g1,mo_twoptp3_g3g1_A,tau, 2, src, path, buf);
    sprintf(buf, "rhop3_g2g3_tau_%d", tau);
	io2pt(mo_twoptp3_g2g3,mo_twoptp3_g2g3_A,tau, 2, src, path, buf);
    sprintf(buf, "rhop3_g3g2_tau_%d", tau);
	io2pt(mo_twoptp3_g3g2,mo_twoptp3_g3g2_A,tau, 2, src, path, buf);
  }

    if(list==NULL) break;
}
  lprintf("DEBUG",0,"ALL done, deallocating\n");

#define X(NAME) free_mo(mo_##NAME);
  OBSERVABLE_LIST
#undef X
  if(list!=NULL) fclose(list);
  finalize_process();
  free_BCs();
  free_gfield(u_gauge);
  free_propagator_eo();

  return 0;
}