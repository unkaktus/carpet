#include <cassert>
#include <sstream>
#include <string>

#include "cctk.h"
#include "cctk_Arguments.h"
#include "cctk_Parameters.h"



namespace CarpetTracker {

using namespace std;
  
  
  
  // Maximum number of tracked surfaces
  int const num_surfaces = 10;
  
  
  
  extern "C" {
    void
    CarpetTracker_SetPositions (CCTK_ARGUMENTS);
  }
  
  
  
  void
  CarpetTracker_SetPositions (CCTK_ARGUMENTS)
  {
    DECLARE_CCTK_ARGUMENTS;
    DECLARE_CCTK_PARAMETERS;
    
    for (int n = 0; n < num_surfaces; ++ n) {
      int const sn = surface[n];
      if (sn >= 0) {
        assert (sn >= 0 and sn < nsurfaces);
        
        if (sf_valid[sn] > 0) {
          
          if (verbose) {
            CCTK_VInfo (CCTK_THORNSTRING,
                        "Setting position of refined region #%d from surface #%d to (%g,%g,%g)",
                        n + 1, sn,
                        static_cast <double> (sf_centroid_x[sn]),
                        static_cast <double> (sf_centroid_y[sn]),
                        static_cast <double> (sf_centroid_z[sn]));
          }
          
          // Set position in CarpetRegrid2
          position_x[n] = sf_centroid_x[sn];
          position_y[n] = sf_centroid_y[sn];
          position_z[n] = sf_centroid_z[sn];
          
        } else {
          
          if (verbose) {
            CCTK_VInfo (CCTK_THORNSTRING,
                        "No position information available for refined region #%d from surface #%d",
                        n + 1, sn);
          }
          
        } // if valid
        
      } // if sn > 0
    } // for n
  }
  
} // namespace CarpetTracker
