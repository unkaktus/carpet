#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "cctk.h"
#include "cctk_Parameters.h"

#include "CactusBase/IOUtil/src/ioGH.h"

#include "data.hh"
#include "dist.hh"
#include "gdata.hh"
#include "gf.hh"
#include "ggf.hh"
#include "vect.hh"

#include "carpet.hh"

#include "ioascii.hh"
  
extern "C" {
  static const char* rcsid = "$Header: /home/eschnett/C/carpet/Carpet/Carpet/CarpetIOASCII/src/ioascii.cc,v 1.54 2003/11/05 16:18:37 schnetter Exp $";
  CCTK_FILEVERSION(Carpet_CarpetIOASCII_ioascii_cc);
}



// That's a hack
namespace Carpet {
  void UnsupportedVarType (const int vindex);
}



namespace CarpetIOASCII {
  
  using namespace std;
  using namespace Carpet;
  
  bool CheckForVariable (const cGH* const cgh,
			 const char* const varlist, const int vindex);
  void SetFlag (int index, const char* optstring, void* arg);
  
  template<int D,int DD>
  void WriteASCII (ostream& os,
		   const gdata<D>* const gfdata,
		   const bbox<int,D>& gfext,
		   const int vi,
		   const int time,
		   const vect<int,D>& org,
		   const vect<int,DD>& dirs,
		   const int tl,
		   const int rl,
		   const int c,
		   const int ml,
		   const CCTK_REAL coord_time,
		   const vect<CCTK_REAL,D>& coord_lower,
		   const vect<CCTK_REAL,D>& coord_upper);
  
  
  
  int CarpetIOASCIIStartup()
  {
    IOASCII<1>::Startup();
    IOASCII<2>::Startup();
    IOASCII<3>::Startup();
    
    return 0;
  }
  
  
  
  // Definition of static members
  template<int outdim> int IOASCII<outdim>::GHExtension;
  template<int outdim> int IOASCII<outdim>::IOMethod;
  template<int outdim> vector<bool> IOASCII<outdim>::do_truncate;
  template<int outdim> vector<vector<int> > IOASCII<outdim>::last_output;
  
  
  
  template<int outdim>
  int IOASCII<outdim>::Startup()
  {
    ostringstream msg;
    msg << "AMR " << outdim << "D ASCII I/O provided by CarpetIOASCII";
    CCTK_RegisterBanner (msg.str().c_str());
    
    ostringstream extension_name;
    extension_name << "CarpetIOASCII_" << outdim << "D";
    GHExtension = CCTK_RegisterGHExtension(extension_name.str().c_str());
    CCTK_RegisterGHExtensionSetupGH (GHExtension, SetupGH);
    
    ostringstream method_name;
    method_name << "IOASCII_" << outdim << "D";
    IOMethod = CCTK_RegisterIOMethod (method_name.str().c_str());
    CCTK_RegisterIOMethodOutputGH (IOMethod, OutputGH);
    CCTK_RegisterIOMethodOutputVarAs (IOMethod, OutputVarAs);
    CCTK_RegisterIOMethodTimeToOutput (IOMethod, TimeToOutput);
    CCTK_RegisterIOMethodTriggerOutput (IOMethod, TriggerOutput);
    
    return 0;
  }
  
  
  
  template<int outdim>
  void* IOASCII<outdim>
  ::SetupGH (tFleshConfig* const fc, const int convLevel, cGH* const cgh)
  {
    DECLARE_CCTK_PARAMETERS;
    
    // Truncate all files if this is not a restart
    do_truncate.resize(CCTK_NumVars(), true);
    
    // No iterations have yet been output
    last_output.resize(maxreflevels);
    for (int rl=0; rl<maxreflevels; ++rl) {
      last_output[rl].resize(CCTK_NumVars(), INT_MIN);
    }
    
    // We register only once, ergo we get only one handle.  We store
    // that statically, so there is no need to pass anything to
    // Cactus.
    return 0;
  }
  
  
  
  template<int outdim>
  int IOASCII<outdim>
  ::OutputGH (const cGH* const cgh)
  {
    for (int vindex=0; vindex<CCTK_NumVars(); ++vindex) {
      if (TimeToOutput(cgh, vindex)) {
	TriggerOutput(cgh, vindex);
      }
    }
    return 0;
  }
  
  
  
  template<int outdim>
  int IOASCII<outdim>
  ::OutputVarAs (const cGH* const cgh,
		 const char* const varname, const char* const alias)
  {
    DECLARE_CCTK_PARAMETERS;
    
    const int n = CCTK_VarIndex(varname);
    assert (n>=0 && n<CCTK_NumVars());
    const int group = CCTK_GroupIndexFromVarI (n);
    assert (group>=0 && group<(int)Carpet::arrdata.size());
    const int n0 = CCTK_FirstVarIndexI(group);
    assert (n0>=0 && n0<CCTK_NumVars());
    const int var = n - n0;
    assert (var>=0 && var<CCTK_NumVars());
    const int num_tl = CCTK_NumTimeLevelsFromVarI(n);
    assert (num_tl>=1);
    
    // Check for storage
    if (! CCTK_QueryGroupStorageI(cgh, group)) {
      CCTK_VWarn (1, __LINE__, __FILE__, CCTK_THORNSTRING,
		  "Cannot output variable \"%s\" because it has no storage",
		  varname);
      return 0;
    }
    
    const int grouptype = CCTK_GroupTypeI(group);
    const int rl = grouptype==CCTK_GF ? reflevel : 0;
    
    // Get grid hierarchy extentsion from IOUtil
    const ioGH * const iogh = (const ioGH *)CCTK_GHExtension (cgh, "IO");
    assert (iogh);
    
    // Create the output directory
    const char* myoutdir = GetStringParameter("out%dD_dir", out_dir);
    if (CCTK_MyProc(cgh)==0) {
      CCTK_CreateDirectory (0755, myoutdir);
    }
    
    // Loop over all direction combinations
    vect<int,outdim> dirs;
    for (int d=0; d<outdim; ++d) dirs[d] = 0;
    
    bool done;
    do {
      
      // Output each combination only once
      bool ascending = true;
      for (int d1=0; d1<outdim; ++d1) {
	for (int d2=d1+1; d2<outdim; ++d2) {
	  ascending = ascending && dirs[d1] < dirs[d2];
	}
      }
      
      // Skip output if the dimensions are not ascending
      if (ascending) {
	
	// If this output is desired at all
	bool desired;
	switch (outdim) {
	case 1:
	  switch (dirs[0]) {
	  case 0:
	    desired = out1D_x;
	    break;
	  case 1:
	    desired = out1D_y;
	    break;
	  case 2:
	    desired = out1D_z;
	    break;
	  default:
	    assert (0);
	  }
	  break;
	case 2:
	  if (dirs[0]==0 && dirs[1]==1) {
	    desired = out2D_xy;
	  } else if (dirs[0]==0 && dirs[1]==2) {
	    desired = out2D_xz;
	  } else if (dirs[0]==1 && dirs[1]==2) {
	    desired = out2D_yz;
	  } else {
	    assert (0);
	  }
	  break;
	case 3:
	  // Output is always desired (if switched on)
	  desired = true;
	  break;
	default:
	  assert (0);
	}
	
	// Skip output if not desired
	if (desired) {
	  
	  // Invent a file name
	  ostringstream filenamebuf;
	  filenamebuf << myoutdir << "/" << alias << ".";
	  for (int d=0; d<outdim; ++d) {
	    assert (dirs[d]>=0 && dirs[d]<3);
	    const char* const coords = "xyz";
	    filenamebuf << coords[dirs[d]];
	  }
	  const char* const suffixes = "lpv";
	  filenamebuf << suffixes[outdim-1];
	  // we need a persistent temporary here
	  string filenamestr = filenamebuf.str();
	  const char* const filename = filenamestr.c_str();
	  
	  ofstream file;
	  
	  if (CCTK_MyProc(cgh)==0) {
	    // If this is the first time, then write a nice header on
	    // the root processor
	    if (do_truncate[n]) {
	      struct stat fileinfo;
	      if (! iogh->recovered
		  || stat(filename, &fileinfo)!=0) {
		file.open (filename, ios::out | ios::trunc);
		assert (file.good());
		file << "# " << varname;
		for (int d=0; d<outdim; ++d) {
		  file << " " << "xyz"[dirs[d]];
		}
		file << " (" << alias << ")" << endl;
		file << "#" << endl;
		assert (file.good());
	      }
	    }
	    if (! file.is_open()) {
	      file.open (filename, ios::out | ios::app);
	      assert (file.good());
	    }
	    file << setprecision(15);
	    assert (file.good());
	  }
	  
	  assert (outdim <= CCTK_GroupDimI(group));
	  
	  // Find the output offset
	  vect<int,dim> offset(0);
	  switch (outdim) {
	  case 1:
	    switch (dirs[0]) {
	    case 0:
	      offset[1] = GetGridOffset (cgh, 2,
					 "out%dD_xline_yi", "out_xline_yi",
					 "out%dD_xline_y",  "out_xline_y",
					 out_xline_y);
	      offset[2] = GetGridOffset (cgh, 3,
					 "out%dD_xline_zi", "out_xline_zi",
					 "out%dD_xline_z",  "out_xline_z",
					 out_xline_z);
	      break;
	    case 1:
	      offset[0] = GetGridOffset (cgh, 1,
					 "out%dD_yline_xi", "out_yline_xi",
					 "out%dD_yline_x",  "out_yline_x",
					 out_yline_x);
	      offset[2] = GetGridOffset (cgh, 3,
					 "out%dD_yline_zi", "out_yline_zi",
					 "out%dD_yline_z",  "out_yline_z",
					 out_yline_z);
	      break;
	    case 2:
	      offset[0] = GetGridOffset (cgh, 1,
					 "out%dD_zline_xi", "out_zline_xi",
					 "out%dD_zline_x",  "out_zline_x",
					 out_zline_x);
	      offset[1] = GetGridOffset (cgh, 2,
					 "out%dD_zline_yi", "out_zline_yi",
					 "out%dD_zline_y",  "out_zline_y",
					 out_zline_y);
	      break;
	    default:
	      assert (0);
	    }
	    break;
	  case 2:
	    if (dirs[0]==0 && dirs[1]==1) {
	      offset[2] = GetGridOffset
		(cgh, 3,
		 "out%dD_xyplane_zi", "out_xyplane_zi",
		 "out%dD_xyplane_z",  "out_xyplane_z",
		 out_xyplane_z);
	    } else if (dirs[0]==0 && dirs[1]==2) {
	      offset[1] = GetGridOffset
		(cgh, 2,
		 "out%dD_xzplane_yi", "out_xzplane_yi",
		 "out%dD_xzplane_y",  "out_xzplane_y",
		 out_xzplane_y);
	    } else if (dirs[0]==1 && dirs[1]==2) {
	      offset[0] = GetGridOffset
		(cgh, 1,
		 "out%dD_yzplane_xi", "out_yzplane_xi",
		 "out%dD_yzplane_x",  "out_yzplane_x",
		 out_yzplane_x);
	    } else {
	      assert (0);
	    }
	    break;
	  case 3:
	    // The offset doesn't matter in this case
	    break;
	  default:
	    assert (0);
	  }
	  
	  // Traverse all components on this refinement and multigrid
	  // level
	  BEGIN_COMPONENT_LOOP(cgh, grouptype) {
	    
	    const ggf<dim>* ff = 0;
	    
	    assert (var < (int)arrdata[group].data.size());
	    ff = (ggf<dim>*)arrdata[group].data[var];
	    
            const int mintl = output_all_timelevels ? 1-num_tl : 0;
            const int maxtl = 0;
            for (int tl=mintl; tl<=maxtl; ++tl) {
              
              const gdata<dim>* const data
                = (*ff) (tl, rl, component, mglevel);
              bbox<int,dim> ext = data->extent();
              
              vect<int,dim> lo = ext.lower();
              vect<int,dim> hi = ext.upper();
              vect<int,dim> str = ext.stride();
              
              // Ignore ghost zones if desired
              for (int d=0; d<dim; ++d) {
                bool output_lower_ghosts
                  = cgh->cctk_bbox[2*d] ? out3D_outer_ghosts : out3D_ghosts;
                bool output_upper_ghosts
                  = cgh->cctk_bbox[2*d+1] ? out3D_outer_ghosts : out3D_ghosts;
                
                if (! output_lower_ghosts) {
                  lo[d] += cgh->cctk_nghostzones[d] * str[d];
                }
                if (! output_upper_ghosts) {
                  hi[d] -= cgh->cctk_nghostzones[d] * str[d];
                }
              }
              ext = bbox<int,dim>(lo,hi,str);
              
              // coordinates
              const CCTK_REAL coord_time = cgh->cctk_time;
              vect<CCTK_REAL,dim> global_lower;
              vect<CCTK_REAL,dim> coord_delta;
              if (grouptype == CCTK_GF) {
                for (int d=0; d<dim; ++d) {
                  global_lower[d] = cgh->cctk_origin_space[d];
                  coord_delta[d] = cgh->cctk_delta_space[d] / maxreflevelfact;
                }
              } else {
                for (int d=0; d<dim; ++d) {
                  global_lower[d] = 0.0;
                  coord_delta[d] = 1.0 / (cgh->cctk_gsh[d] - 1);
                }
              }
              // Note: don't permute the "coord_delta" and
              // "data->extent().lower()"
              // (it seems that for gcc 2.95 you then pick up the
              // integer operator*)
              const vect<CCTK_REAL,dim> coord_lower = global_lower + coord_delta * vect<CCTK_REAL,dim>(lo);
              const vect<CCTK_REAL,dim> coord_upper = global_lower + coord_delta * vect<CCTK_REAL,dim>(hi);
              
              vect<int,dim> offset1;
              for (int d=0; d<dim; ++d) {
                assert (cgh->cctk_levoffdenom[d]==1);
                offset1[d] = (cgh->cctk_levoff[d] + offset[d]) * ext.stride()[d];
              }
              for (int d=0; d<outdim; ++d) {
                offset1[dirs[d]] = ext.lower()[dirs[d]];
              }
              
              WriteASCII (file, data, ext, n, cgh->cctk_iteration, offset1, dirs,
                          tl, rl, component, mglevel,
                          coord_time, coord_lower, coord_upper);
              
              // Append EOL after every component
              if (CCTK_MyProc(cgh)==0) {
                if (separate_components) {
                  assert (file.good());
                  file << endl;
                }
              }
              assert (file.good());
              
            } // for tl
	    
	  } END_COMPONENT_LOOP;
	  
	  // Append EOL after every complete set of components
	  if (CCTK_MyProc(cgh)==0) {
	    if (separate_grids) {
	      assert (file.good());
	      file << endl;
	    }
	    file.close();
	    assert (file.good());
	  }
	  
	  assert (! file.is_open());
	  
	} // if (desired)
	
      } // if (ascending)
      
      // Next direction combination
      done = true;
      for (int d=0; d<outdim; ++d) {
	++dirs[d];
	if (dirs[d]<CCTK_GroupDimI(group)) {
	  done = false;
	  break;
	}
	dirs[d] = 0;
      }
      
    } while (! done);		// all directions
    
    // Don't truncate again
    do_truncate[n] = false;
    
    return 0;
  }
  
  
  
  template<int outdim>
  int IOASCII<outdim>
  ::TimeToOutput (const cGH* const cgh, const int vindex)
  {
    DECLARE_CCTK_PARAMETERS;
    
    assert (vindex>=0 && vindex<CCTK_NumVars());
    
    const int myoutevery = GetIntParameter("out%dD_every", out_every);
    
    if (myoutevery <= 0) {
      // Nothing should be output at all
      return 0;
    }
    
    if (cgh->cctk_iteration % myoutevery != 0) {
      // Nothing should be output during this iteration
      return 0;
    }
    
    if (! CheckForVariable(cgh, GetStringParameter("out%dD_vars", ""), vindex)) {
      // This variable should not be output
      return 0;
    }
    
    if (last_output[reflevel][vindex] == cgh->cctk_iteration) {
      // Has already been output during this iteration
      char* varname = CCTK_FullName(vindex);
      CCTK_VWarn (5, __LINE__, __FILE__, CCTK_THORNSTRING,
		  "Skipping output for variable \"%s\", because this variable "
		  "has already been output during the current iteration -- "
		  "probably via a trigger during the analysis stage",
		  varname);
      free (varname);
      return 0;
    }
    
    assert (last_output[reflevel][vindex] < cgh->cctk_iteration);
    
    // Should be output during this iteration
    return 1;
  }
  
  
  
  template<int outdim>
  int IOASCII<outdim>
  ::TriggerOutput (const cGH* const cgh, const int vindex)
  {
    assert (vindex>=0 && vindex<CCTK_NumVars());
    
    char* varname = CCTK_FullName(vindex);
    const int retval = OutputVarAs (cgh, varname, CCTK_VarName(vindex));
    free (varname);
    
    last_output[reflevel][vindex] = cgh->cctk_iteration;
    
    return retval;
  }
  
  
  
  template<int outdim>
  int IOASCII<outdim>
  ::GetGridOffset (const cGH* const cgh, const int dir,
		   const char* const itempl, const char* const iglobal,
		   const char* const ctempl, const char* const cglobal,
		   const CCTK_REAL cfallback)
  {
    // First choice: explicit coordinate
    char cparam[1000];
    sprintf (cparam, ctempl, outdim);
    const int ncparam = CCTK_ParameterQueryTimesSet (cparam, CCTK_THORNSTRING);
    assert (ncparam >= 0);
    if (ncparam > 0) {
      int ptype;
      const CCTK_REAL* const pcoord
	= ((const CCTK_REAL*)CCTK_ParameterGet
           (cparam, CCTK_THORNSTRING, &ptype));
      assert (pcoord);
      const CCTK_REAL coord = *pcoord;
      assert (ptype == PARAMETER_REAL);
      return CoordToOffset (cgh, dir, coord, 0);
    }
    
    // Second choice: explicit index
    char iparam[1000];
    sprintf (iparam, itempl, outdim);
    const int niparam = CCTK_ParameterQueryTimesSet (iparam, CCTK_THORNSTRING);
    assert (niparam >= 0);
    if (niparam > 0) {
      int ptype;
      const int* const pindex
	= (const int*)CCTK_ParameterGet (iparam, CCTK_THORNSTRING, &ptype);
      assert (pindex);
      const int index = *pindex;
      assert (ptype == PARAMETER_INT);
      return index;
    }
    
    // Third choice: explicit global coordinate
    const char* iothorn = CCTK_ImplementationThorn ("IO");
    assert (iothorn);
    const int ncglobal = CCTK_ParameterQueryTimesSet (cglobal, iothorn);
    assert (ncglobal >= 0);
    if (ncglobal > 0) {
      int ptype;
      const CCTK_REAL* const pcoord
	= (const CCTK_REAL*)CCTK_ParameterGet (cglobal, iothorn, &ptype);
      assert (pcoord);
      const CCTK_REAL coord = *pcoord;
      assert (ptype == PARAMETER_REAL);
      return CoordToOffset (cgh, dir, coord, 0);
    }
    
    // Fourth choice: explicit global index
    const int niglobal = CCTK_ParameterQueryTimesSet (iglobal, iothorn);
    assert (niglobal >= 0);
    if (niglobal > 0) {
      int ptype;
      const int* const pindex
	= (const int*)CCTK_ParameterGet (iglobal, iothorn, &ptype);
      assert (pindex);
      const int index = *pindex;
      assert (ptype == PARAMETER_INT);
      return index;
    }
    
    // Fifth choice: default coordinate
    return CoordToOffset (cgh, dir, cfallback, 0);
  }
  
  
  
  template<int outdim>
  int IOASCII<outdim>
  ::CoordToOffset (const cGH* cgh, const int dir, const CCTK_REAL coord,
		   const int ifallback)
  {
    assert (dir>=1 && dir<=dim);
    
    assert (reflevel!=-1 && mglevel!=-1);
    
    const int npoints = cgh->cctk_gsh[dir-1];
    const CCTK_REAL delta = cgh->cctk_delta_space[dir-1] / cgh->cctk_levfac[dir-1];
    const CCTK_REAL lower = cgh->cctk_origin_space[dir-1] + delta * cgh->cctk_levoff[dir-1] / cgh->cctk_levoffdenom[dir-1];
    const CCTK_REAL upper = lower + (npoints-1) * delta;
    
    const CCTK_REAL rindex = (coord - lower) / delta;
    int cindex = (int)floor(rindex + 0.75);
    
    if (cindex<0 || cindex>=npoints) {
      CCTK_VWarn (1, __LINE__, __FILE__, CCTK_THORNSTRING,
		  "The specified coordinate value %g is not within the grid range [%g,%g]",
		  coord, lower, upper);
      cindex = ifallback;
    }
    
    assert (cindex>=0 && cindex<npoints);
    
    return cindex;
  }
  
  
  
  template<int outdim>
  const char* IOASCII<outdim>
  ::GetStringParameter (const char* const parametertemplate,
			const char* const fallback)
  {
    char parametername[1000];
    sprintf (parametername, parametertemplate, outdim);
    const int ntimes =
      CCTK_ParameterQueryTimesSet (parametername, CCTK_THORNSTRING);
    assert (ntimes >= 0);
    if (ntimes > 0) {
      int ptype;
      const char* const* const ppval = (const char* const*)CCTK_ParameterGet
	(parametername, CCTK_THORNSTRING, &ptype);
      assert (ppval);
      const char* const pval = *ppval;
      assert (ptype == PARAMETER_STRING);
      return pval;
    }
    
    return fallback;
  }
  
  
  
  template<int outdim>
  int IOASCII<outdim>
  ::GetIntParameter (const char* const parametertemplate, const int fallback)
  {
    char parametername[1000];
    sprintf (parametername, parametertemplate, outdim);
    const int ntimes =
      CCTK_ParameterQueryTimesSet (parametername, CCTK_THORNSTRING);
    assert (ntimes >= 0);
    if (ntimes > 0) {
      int ptype;
      const int* const ppval
	= (const int*)CCTK_ParameterGet
        (parametername, CCTK_THORNSTRING, &ptype);
      assert (ppval);
      const int pval = *ppval;
      assert (ptype == PARAMETER_INT);
      return pval;
    }
    
    return fallback;
  }
  
  
  
  bool CheckForVariable (const cGH* const cgh,
			 const char* const varlist, const int vindex)
  {
    const int numvars = CCTK_NumVars();
    assert (vindex>=0 && vindex<numvars);
    
    vector<bool> flags(numvars);
    
    CCTK_TraverseString (varlist, SetFlag, &flags, CCTK_GROUP_OR_VAR);
    
    return flags[vindex];
  }
  
  void SetFlag (int index, const char* optstring, void* arg)
  {
    vector<bool>& flags = *(vector<bool>*)arg;
    flags[index] = true;
  }
  
  
  
  // Output
  template<int D,int DD>
  void WriteASCII (ostream& os,
		   const gdata<D>* const gfdata,
		   const bbox<int,D>& gfext,
		   const int vi,
		   const int time,
		   const vect<int,D>& org,
		   const vect<int,DD>& dirs,
		   const int tl,
		   const int rl,
		   const int c,
		   const int ml,
		   const CCTK_REAL coord_time,
		   const vect<CCTK_REAL,D>& coord_lower,
		   const vect<CCTK_REAL,D>& coord_upper)
  {
    assert (DD<=D);
    
    if (gfdata->proc()==0) {
      // output on processor 0
      
      int rank;
      MPI_Comm_rank (dist::comm, &rank);
      if (rank == 0) {
	
	assert (os.good());
        
	os << "# iteration " << time << endl
	   << "# time level " << tl << "   refinement level " << rl
	   << "   component " << c << "   multigrid level " << ml << endl
	   << "# column format: it   tl rl c ml  ";
	assert (D>=1 && D<=3);
	const char* const coords = "xyz";
	for (int d=0; d<D; ++d) os << " i" << coords[d];
	os << "   time  ";
	for (int d=0; d<D; ++d) os << " " << coords[d];
	os << "   data" << endl;
	
	const vect<int,DD> lo = gfext.lower()[dirs];
	const vect<int,DD> up = gfext.upper()[dirs];
	const vect<int,DD> str = gfext.stride()[dirs];
	const bbox<int,DD> ext(lo,up,str);
	
	// Check whether the output origin is contained in the extent
	// of the data that should be output
	vect<int,dim> org1(org);
	for (int d=0; d<DD; ++d) org1[dirs[d]] = ext.lower()[d];
	if (gfext.contains(org1)) {
	  
	  for (typename bbox<int,DD>::iteratorT it=ext.beginT();
	       it!=ext.endT(); ++it) {
	    vect<int,dim> index(org);
	    for (int d=0; d<DD; ++d) index[dirs[d]] = (*it)[d];
	    os << time << "   " << tl << " " << rl << " " << c << " " << ml
	       << "   ";
	    for (int d=0; d<D; ++d) os << index[d] << " ";
	    os << "  " << coord_time << "   ";
	    for (int d=0; d<D; ++d) {
	      assert (gfext.upper()[d] - gfext.lower()[d] != 0);
	      os << (coord_lower[d] + (index[d] - gfext.lower()[d])
		     * (coord_upper[d] - coord_lower[d])
		     / (gfext.upper()[d] - gfext.lower()[d])) << " ";
	    }
	    os << "  ";
	    switch (CCTK_VarTypeI(vi)) {
#define WANT_NO_COMPLEX
#define TYPECASE(N,T)					\
	    case N:					\
	      os << (*(const data<T,D>*)gfdata)[index];	\
	      break;
#include "Carpet/Carpet/src/typecase"
#undef TYPECASE
#undef WANT_NO_COMPLEX
#define WANT_NO_INT
#define WANT_NO_REAL
#define TYPECASE(N,T)					                \
	    case N:					                \
	      os << (*(const data<T,D>*)gfdata)[index].real() << " "    \
                 << (*(const data<T,D>*)gfdata)[index].imag();	        \
	      break;
#include "Carpet/Carpet/src/typecase"
#undef TYPECASE
#undef WANT_NO_INT
#undef WANT_NO_REAL
	    default:
	      UnsupportedVarType(vi);
	    }
	    os << endl;
	    for (int d=DD-1; d>=0; --d) {
	      if (index[dirs[d]]!=gfext.upper()[dirs[d]]) break;
	      os << endl;
	    }
	  }
	  
	} else {
	  
	  os << "#" << endl;
	  
	} // if ! ext contains org
	
	assert (os.good());
	
      }
      
    } else {
      // copy to processor 0 and output there
      
      gdata<D>* const tmp = gfdata->make_typed(vi);
      tmp->allocate(gfdata->extent(), 0);
      for (comm_state<dim> state; !state.done(); state.step()) {
        tmp->copy_from (state, gfdata, gfdata->extent());
      }
      WriteASCII (os, tmp, gfext, vi, time, org, dirs, tl, rl, c, ml,
		  coord_time, coord_lower, coord_upper);
      delete tmp;
      
    }
  }
  
  
  
  
  
  // Explicit instantiation for all output dimensions
  template class IOASCII<1>;
  template class IOASCII<2>;
  template class IOASCII<3>;
  
  template
  void WriteASCII (ostream& os,
		   const gdata<3>* const gfdata,
		   const bbox<int,3>& gfext,
		   const int vi,
		   const int time,
		   const vect<int,3>& org,
		   const vect<int,1>& dirs,
		   const int tl,
		   const int rl,
		   const int c,
		   const int ml,
		   const CCTK_REAL coord_time,
		   const vect<CCTK_REAL,3>& coord_lower,
		   const vect<CCTK_REAL,3>& coord_upper);
  
  template
  void WriteASCII (ostream& os,
		   const gdata<3>* const gfdata,
		   const bbox<int,3>& gfext,
		   const int vi,
		   const int time,
		   const vect<int,3>& org,
		   const vect<int,2>& dirs,
		   const int tl,
		   const int rl,
		   const int c,
		   const int ml,
		   const CCTK_REAL coord_time,
		   const vect<CCTK_REAL,3>& coord_lower,
		   const vect<CCTK_REAL,3>& coord_upper);

  template
  void WriteASCII (ostream& os,
		   const gdata<3>* const gfdata,
		   const bbox<int,3>& gfext,
		   const int vi,
		   const int time,
		   const vect<int,3>& org,
		   const vect<int,3>& dirs,
		   const int tl,
		   const int rl,
		   const int c,
		   const int ml,
		   const CCTK_REAL coord_time,
		   const vect<CCTK_REAL,3>& coord_lower,
		   const vect<CCTK_REAL,3>& coord_upper);

} // namespace CarpetIOASCII
