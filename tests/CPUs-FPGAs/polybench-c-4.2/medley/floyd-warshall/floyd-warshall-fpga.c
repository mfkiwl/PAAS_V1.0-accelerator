/**
 * This version is stamped on May 10, 2016
 *
 * Contact:
 *   Louis-Noel Pouchet <pouchet.ohio-state.edu>
 *   Tomofumi Yuki <tomofumi.yuki.fr>
 *
 * Web address: http://polybench.sourceforge.net
 */
/* floyd-warshall.c: this file is part of PolyBench/C */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

/* Include polybench common header. */
#include "../../utilities/polybench.h"

/* Include benchmark-specific header. */
#include "floyd-warshall.h"
#include <new>
using namespace std;
unsigned long long* p0;
int amo,t2;
int v[32888];
int u[9][N][N];
bool done[145];
long long q[2*161][161];
/* Array initialization. */
void try_fpga()
{
	//printf("fpga amount=:%d\n",amo);
	if (!p0[6])
	{
		if (amo==71) return;
		while (done[t2]) {t2++;t2=t2%72;}
		done[t2]=1;
		printf("fpga processing %d done[%d]=%d\n",t2,t2,done[t2]);
    	p0[1] = (unsigned long long)u[t2];//ReadBase
		p0[2] = (unsigned long long)u[t2];//WriteBase
		amo++;		//printf("amount=:%d\n",amo);
  		p0[6]=0;p0[6]=1;
		
	}
}
/* Array initialization. */
static
void init_array (int n,
		 DATA_TYPE POLYBENCH_2D(path,N,N,n,n),int p)
{
  int i, j;

  for (i = 0; i < n; i++)
    for (j = 0; j < n; j++) {
      path[i][j] = (i+n)*(j+n)+7+p;
  //    if ((i+j)%13 == 0 || (i+j)%7==0 || (i+j)%11 == 0)
   //      path[i][j] = 999;
    }
}


/* DCE code. Must scan the entire live-out data.
   Can be used also to check the correctness of the output. */
static
void print_array(int n,
		 DATA_TYPE POLYBENCH_2D(path,N,N,n,n))

{
  int i, j;

  POLYBENCH_DUMP_START;
  POLYBENCH_DUMP_BEGIN("path");
  for (i = 0; i < n; i++)
    for (j = 0; j < n; j++) {
      if ((i * n + j) % 20 == 0) fprintf (POLYBENCH_DUMP_TARGET, "\n");
      fprintf (POLYBENCH_DUMP_TARGET, DATA_PRINTF_MODIFIER, path[i][j]);
    }
  POLYBENCH_DUMP_END("path");
  POLYBENCH_DUMP_FINISH;
}


/* Main computational kernel. The whole function will be timed,
   including the call and return. */
//static
void kernel_floyd_warshall(int path[N][N])
			//   DATA_TYPE POLYBENCH_2D(path,N,N,n,n))
{
  int i, j, k;
  int n=N;
  kernel_floyd_warshall_label0:for (k = 0; k < _PB_N; k++)
    {try_fpga();
      kernel_floyd_warshall_label1:for(i = 0; i < _PB_N; i++)
	{
	kernel_floyd_warshall_label2:for (j = 0; j < _PB_N; j++)
	  path[i][j] = (path[i][j] < path[i][k] + path[k][j]) ?
	    (path[i][j]) : (path[i][k] + path[k][j]);
	}
    }

}


int main(int argc, char** argv)
{
  /* Retrieve problem size. */
  int n = N;

  /* Variable declaration/allocation. */
  int (*path)[N];
	//printf("111111");

  p0 =(unsigned long long *) new((unsigned long long *)0xffff00000000) unsigned long long[10];//Contro Port
		p0[0] = 81;	
	p0[8] = 1;
	p0[3] = 9;//CurrentThreadID
	p0[4] = 64*64;//Memory Range
	p0[5] = 4;//MemorySize
	p0[6] = 0;
	p0[7] = 0;//Terminat
  /* Initialize array(s). */
    t2=0;amo=0;
    int i;
  int qq;
  for (qq=1;qq<=19;qq++)
{
	for (i=1;i<=8;i++)	
	{
		init_array (n, u[i],i);
	}

  /* Start timer. */
  polybench_start_instruments;
	int tmp=0,tmpp;
  int rc=0,prepare=0;i=0;

	for (i=1;i<=8;i++)
	{
		
    	p0[1] = (unsigned long long)u[i];//ReadBase
		p0[2] = (unsigned long long)u[i];//WriteBase
  		p0[6]=0;p0[6]=1;//printf("fpga processing %d  %llu %d\n",i,(unsigned long long)p0[1],tmpp);
		while(p0[6]); //{tmpp=(tmp+33)%(32888);v[tmp]=v[tmpp]+21;tmp=tmpp;};
	}
}
  //p0[6]=0;p0[6]=1;while(p0[6]);
  /* Stop and print timer. */
  polybench_stop_instruments;
  polybench_print_instruments;

  /* Prevent dead-code elimination. All live-out data must be printed
     by the function call in argument. */
//  polybench_prevent_dce(print_array(n, u[3]));
  /* Be clean. */
  //POLYBENCH_FREE_ARRAY(path);

  return 0;
}
