/* $Header: /home/eschnett/C/carpet/Carpet/Carpet/CarpetIOHDF5/src/iohdf5.h,v 1.3 2004/03/11 09:33:23 cott Exp $ */

#ifndef CARPETIOHDF5_H
#define CARPETIOHDF5_H

#include "cctk_Arguments.h"

#ifdef __cplusplus
namespace CarpetIOHDF5 {
  extern "C" {
#endif
    
    /* Scheduled functions */
    int CarpetIOHDF5Startup (void);
    int CarpetIOHDF5ReadData (CCTK_ARGUMENTS);
    void CarpetIOHDF5_EvolutionCheckpoint (const cGH*);
    
    int CarpetIOHDF5_Recover (cGH* cgh, const char *basefilename, 
			      int called_from);

    int CarpetIOHDF5_RecoverParameters (void);

#ifdef __cplusplus
  } /* extern "C" */
} /* namespace CarpetIOHDF5 */
#endif

#endif /* !defined(CARPETIOHDF5_H) */
