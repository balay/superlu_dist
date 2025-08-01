/*! \file
Copyright (c) 2003, The Regents of the University of California, through
Lawrence Berkeley National Laboratory (subject to receipt of any required
approvals from U.S. Dept. of Energy)

All rights reserved.

The source code is distributed under BSD license, see the file License.txt
at the top-level directory.
*/


/*! @file
 * \brief
 * Contributed by Francois-Henry Rouet.
 *
 */
#include <ctype.h>
#include <stdio.h>
#include "superlu_zdefs.h"

#undef EXPAND_SYM

/*! brief
 *
 * <pre>
 * Output parameters
 * =================
 *   (nzval, rowind, colptr): (*rowind)[*] contains the row subscripts of
 *      nonzeros in columns of matrix A; (*nzval)[*] the numerical values;
 *	column i of A is given by (*nzval)[k], k = (*rowind)[i],...,
 *      (*rowind)[i+1]-1.
 * </pre>
 */

void
zreadMM_dist(FILE *fp, int_t *m, int_t *n, int_t *nonz,
	    doublecomplex **nzval, int_t **rowind, int_t **colptr)
{
    int_t    j, k, jsize, nnz, nz, new_nonz;
    doublecomplex *a, *val;
    int_t    *asub, *xa, *row, *col;
    int_t    zero_base = 0;
    char *p, line[512], banner[64], mtx[64], crd[64], arith[64], sym[64];
    int expand;
    char *cs;

    /* 	File format:
     *    %%MatrixMarket matrix coordinate complex general/symmetric/...
     *    % ...
     *    % (optional comments)
     *    % ...
     *    #rows  #cols  #non-zeros
     *    Triplet in the rest of lines: row    col    value
     */

     /* 1/ read header */
     cs = fgets(line,512,fp);
     for (p=line; *p!='\0'; *p=tolower(*p),p++);

     if (sscanf(line, "%s %s %s %s %s", banner, mtx, crd, arith, sym) != 5) {
       printf("Invalid header (first line does not contain 5 tokens)\n");
       exit(-1);
     }

     if(strcmp(banner,"%%matrixmarket")) {
       printf("Invalid header (first token is not \"%%%%MatrixMarket\")\n");
       exit(-1);
     }

     if(strcmp(mtx,"matrix")) {
       printf("Not a matrix; this driver cannot handle that.\n");
       exit(-1);
     }

     if(strcmp(crd,"coordinate")) {
       printf("Not in coordinate format; this driver cannot handle that.\n");
       exit(-1);
     }

     if(strcmp(arith,"complex")) {
       if(!strcmp(arith,"real")) {
         printf("Complex matrix; use dreadMM instead!\n");
         exit(-1);
       }
       else if(!strcmp(arith, "pattern")) {
         printf("Pattern matrix; values are needed!\n");
         exit(-1);
       }
       else {
         printf("Unknown arithmetic\n");
         exit(-1);
       }
     }

     if ( (!strcmp(sym,"symmetric")) || (!strcmp(sym,"hermitian")) ) {
         printf("Symmetric matrix: will be expanded\n");
         expand=1;
     } else expand=0;

     /* 2/ Skip comments */
     while(banner[0]=='%') {
       cs = fgets(line,512,fp);
       sscanf(line,"%s",banner);
     }

     /* 3/ Read n and nnz */
#ifdef _LONGINT
    sscanf(line, "%lld%lld%lld", m, n, nonz);
#else
    sscanf(line, "%d%d%d",m, n, nonz);
#endif

    if(*m!=*n) {
      printf("Rectangular matrix!. Abort\n");
      exit(-1);
   }

    if(expand) {
        new_nonz = 2 * *nonz; /* upper bound, accommodate hard zeros on the diagonal */
	printf("new_nonz upper bound symmetric expansion:\t" IFMT "\n", new_nonz);
    } else {
        new_nonz = *nonz;
    }

    *m = *n;
    printf("m %lld, n %lld, nonz %lld\n", (long long) *m, (long long) *n, (long long) *nonz);
    fflush(stdout);
    zallocateA_dist(*n, new_nonz, nzval, rowind, colptr); /* Allocate storage */
    a    = *nzval;
    asub = *rowind;
    xa   = *colptr;

    if ( !(val = doublecomplexMalloc_dist(new_nonz)) )
        ABORT("Malloc fails for val[]");
    if ( !(row = (int_t *) intMalloc_dist(new_nonz)) )
        ABORT("Malloc fails for row[]");
    if ( !(col = (int_t *) intMalloc_dist(new_nonz)) )
        ABORT("Malloc fails for col[]");

    for (j = 0; j < *n; ++j) xa[j] = 0;

    /* 4/ Read triplets of values */
    for (nnz = 0, nz = 0; nnz < *nonz; ++nnz) {

	j = fscanf(fp, IFMT IFMT "%lf%lf\n", &row[nz], &col[nz], &val[nz].r, &val[nz].i);

	if ( nnz == 0 ) /* first nonzero */ {
	    if ( row[0] == 0 || col[0] == 0 ) {
		zero_base = 1;
		printf("triplet file: row/col indices are zero-based.\n");
	    } else
		printf("triplet file: row/col indices are one-based.\n");
	    fflush(stdout);
	}

	if ( !zero_base ) {
	    /* Change to 0-based indexing. */
	    --row[nz];
	    --col[nz];
	}

	if (row[nz] < 0 || row[nz] >= *m || col[nz] < 0 || col[nz] >= *n
	    /*|| val[nz] == 0.*/) {
	    fprintf(stderr, "nz " IFMT ", (" IFMT ", " IFMT ") = {%e\t%e} out of bound, removed\n",
		    nz, row[nz], col[nz], val[nz].r, val[nz].i);
	    exit(-1);
	} else {
	    ++xa[col[nz]];
            if(expand) {
	        if ( row[nz] != col[nz] ) { /* Excluding diagonal */
	          ++nz;
	          row[nz] = col[nz-1];
	          col[nz] = row[nz-1];
	          val[nz] = val[nz-1];
	          ++xa[col[nz]];
	        }
            }
	    ++nz;
	}
    }

    *nonz = nz;
    if(expand) {
      printf("*nonz after symmetric expansion:\t" IFMT "\n", *nonz);
      fflush(stdout);
    }


    /* Initialize the array of column pointers */
    k = 0;
    jsize = xa[0];
    xa[0] = 0;
    for (j = 1; j < *n; ++j) {
	k += jsize;
	jsize = xa[j];
	xa[j] = k;
    }

    /* Copy the triplets into the column oriented storage */
    for (nz = 0; nz < *nonz; ++nz) {
	j = col[nz];
	k = xa[j];
	asub[k] = row[nz];
	a[k] = val[nz];
	++xa[j];
    }

    /* Reset the column pointers to the beginning of each column */
    for (j = *n; j > 0; --j)
	xa[j] = xa[j-1];
    xa[0] = 0;

    SUPERLU_FREE(val);
    SUPERLU_FREE(row);
    SUPERLU_FREE(col);

#ifdef CHK_INPUT
    int i;
    for (i = 0; i < *n; i++) {
	printf("Col %d, xa %d\n", i, xa[i]);
	for (k = xa[i]; k < xa[i+1]; k++)
	    printf("%d\t%16.10f\n", asub[k], a[k]);
    }
#endif

}


static void zreadrhs(int m, doublecomplex *b)
{
    FILE *fp;
    int i;

    if ( !(fp = fopen("b.dat", "r")) ) {
        fprintf(stderr, "zreadrhs: file does not exist\n");
	exit(-1);
    }
    for (i = 0; i < m; ++i)
      i = fscanf(fp, "%lf%lf\n", &b[i].r, &b[i].i);
    fclose(fp);
}
