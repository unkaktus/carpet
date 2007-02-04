#include <cassert>

#include <mpi.h>

#include "cctk.h"
#include "cctk_Parameters.h"

#include "defs.hh"

#include "dist.hh"

using namespace std;



namespace dist {
  
  MPI_Comm comm_ = MPI_COMM_NULL;
  
  MPI_Datatype mpi_complex8;
  MPI_Datatype mpi_complex16;
  MPI_Datatype mpi_complex32;
  
  void init (int& argc, char**& argv) {
    MPI_Init (&argc, &argv);
    pseudoinit (MPI_COMM_WORLD);
  }
  
  void pseudoinit (MPI_Comm const c) {
    comm_ = c;
    
#ifdef HAVE_CCTK_REAL4
    CCTK_REAL4 dummy4;
    MPI_Type_contiguous (2, datatype(dummy4), &mpi_complex8);
    MPI_Type_commit (&mpi_complex8);
#endif
#ifdef HAVE_CCTK_REAL8
    CCTK_REAL8 dummy8;
    MPI_Type_contiguous (2, datatype(dummy8), &mpi_complex16);
    MPI_Type_commit (&mpi_complex16);
#endif
#ifdef HAVE_CCTK_REAL16
    CCTK_REAL16 dummy16;
    MPI_Type_contiguous (2, datatype(dummy16), &mpi_complex32);
    MPI_Type_commit (&mpi_complex32);
#endif
  }
  
  void finalize () {
    MPI_Finalize ();
  }
  
  void checkpoint (const char* file, int line) {
    DECLARE_CCTK_PARAMETERS;
    if (verbose) {
      int rank;
      MPI_Comm_rank (comm(), &rank);
      printf ("CHECKPOINT: processor %d, file %s, line %d\n",
	      rank, file, line);
    }
    if (barriers) {
      MPI_Barrier (comm());
    }
  }
  
} // namespace dist
