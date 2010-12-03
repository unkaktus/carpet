#include <cctk.h>

#include <string>
#include <vector>

#ifdef CCTK_MPI
#  include <mpi.h>
#else
#  include "nompi.h"
#endif



namespace CarpetLib
{
  
  using namespace std;
  
  
  
  // String communication
  
  vector <string>
  gather_string (MPI_Comm comm, int root,
                 string const & data);
  
  vector <string>
  allgather_string (MPI_Comm comm,
                    string const & data);
  
  vector <string>
  alltoallv_string (MPI_Comm comm,
                    vector <string> const & data);
  
  string
  broadcast_string (MPI_Comm comm, int root,
                    string const & data);
  
  
  
  // Arbitrary datatypes
  
  template <typename T>
  vector <vector <T> >
  allgatherv (MPI_Comm comm,
              vector <T> const & data);
  
  template <typename T>
  vector <T>
  alltoall (MPI_Comm comm,
            vector <T> const & data);
  
  template <typename T>
  vector <vector <T> >
  alltoallv (MPI_Comm comm,
             vector <vector <T> > const & data);
  
  template <typename T>
  vector <T>
  alltoallv1 (MPI_Comm comm,
              vector <vector <T> > const & data);
  
} // namespace CarpetLib
