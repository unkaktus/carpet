#include <assert.h>
#include <stdlib.h>

#include <iostream>

#include "cctk.h"
#include "cctk_Parameters.h"
#include "cctki_GHExtensions.h"
#include "cctki_ScheduleBindings.h"
#include "cctki_WarnLevel.h"

#include "carpet.hh"

extern "C" {
  static const char* rcsid = "$Header: /home/eschnett/C/carpet/Carpet/Carpet/Carpet/src/Initialise.cc,v 1.31 2003/08/10 12:52:09 schnetter Exp $";
  CCTK_FILEVERSION(Carpet_Carpet_Initialise_cc);
}



namespace Carpet {
  
  using namespace std;
  
  
  
  void Initialise3TL (cGH* const cgh);
  
  
  
  int Initialise (tFleshConfig* fc)
  {
    DECLARE_CCTK_PARAMETERS;
    
    // Initialise stuff
    const int convlev = 0;
    cGH* const cgh = CCTK_SetupGH (fc, convlev);
    CCTKi_AddGH (fc, convlev, cgh);
    
    // Delay checkpoint until MPI has been initialised
    Waypoint ("starting Initialise...");
    
    // Initialise stuff
    cgh->cctk_iteration = 0;
    do_global_mode = true;
    
    // Enable storage and communtication
    CCTKi_ScheduleGHInit (cgh);
    
    // Initialise stuff
    CCTKi_InitGHExtensions (cgh);
    
    // Check parameters
    Waypoint ("Current time is %g", cgh->cctk_time);
    Waypoint ("PARAMCHECK");
    CCTK_ScheduleTraverse ("CCTK_PARAMCHECK", cgh, CallFunction);
    CCTKi_FinaliseParamWarn();
    
    Waypoint ("Initialising iteration %d...", cgh->cctk_iteration);
    
    
    
    BEGIN_REFLEVEL_LOOP(cgh) {
      
      BEGIN_MGLEVEL_LOOP(cgh) {
	
        cgh->cctk_time = cctk_initial_time + tt->time (0, reflevel, mglevel) * cgh->cctk_delta_time;
        do_global_mode = reflevel == 0;
        
        Waypoint ("%*sCurrent time is %g, delta is %g%s", 2*reflevel, "",
                  cgh->cctk_time,
                  cgh->cctk_delta_time / cgh->cctk_timefac,
                  do_global_mode ? "   (global time)" : "");
	
	// Checking
	Poison (cgh, alltimes);
	
	// Set up the grid
	Waypoint ("%*sScheduling BASEGRID", 2*reflevel, "");
	CCTK_ScheduleTraverse ("CCTK_BASEGRID", cgh, CallFunction);
	
        if (! init_each_timelevel) {
          
          // Set up the initial data
          Waypoint ("%*sScheduling INITIAL", 2*reflevel, "");
          CCTK_ScheduleTraverse ("CCTK_INITIAL", cgh, CallFunction);
          Waypoint ("%*sScheduling POSTINITIAL", 2*reflevel, "");
          CCTK_ScheduleTraverse ("CCTK_POSTINITIAL", cgh, CallFunction);
          
        } else {
          // init_each_timelevel
          
          bool const saved_do_global_mode = do_global_mode;
          
	  tt->set_delta
            (reflevel, mglevel, - tt->get_delta (reflevel, mglevel));
	  tt->advance_time (reflevel, mglevel);
	  tt->advance_time (reflevel, mglevel);
	  tt->advance_time (reflevel, mglevel);
	  tt->set_delta
            (reflevel, mglevel, - tt->get_delta (reflevel, mglevel));
          
          for (int tl=-2; tl<=0; ++tl) {
            
            do_global_mode = saved_do_global_mode && tl==0;
            
            // Advance level times
            tt->advance_time (reflevel, mglevel);
            cgh->cctk_time = cctk_initial_time + tt->time (0, reflevel, mglevel) * cgh->cctk_delta_time;
            
            // Cycle time levels
            CycleTimeLevels (cgh);
            
            Waypoint ("%*sCurrent time is %g, delta is %g%s", 2*reflevel, "",
                      cgh->cctk_time,
                      cgh->cctk_delta_time / cgh->cctk_timefac,
                      do_global_mode ? "   (global time)" : "");
            
            // Set up the initial data
            Waypoint ("%*sScheduling INITIAL", 2*reflevel, "");
            CCTK_ScheduleTraverse ("CCTK_INITIAL", cgh, CallFunction);
            Waypoint ("%*sScheduling POSTINITIAL", 2*reflevel, "");
            CCTK_ScheduleTraverse ("CCTK_POSTINITIAL", cgh, CallFunction);
            
          } // for tl
          
          do_global_mode = saved_do_global_mode;
          
        } // init_each_timelevel
	
	// Poststep
	Waypoint ("%*sScheduling POSTSTEP", 2*reflevel, "");
	CCTK_ScheduleTraverse ("CCTK_POSTSTEP", cgh, CallFunction);
	
	// Checking
	PoisonCheck (cgh, alltimes);
	
      } END_MGLEVEL_LOOP;
      
      // Regrid
      Waypoint ("%*sRegrid", 2*reflevel, "");
      Regrid (cgh, reflevel);
      
    } END_REFLEVEL_LOOP;
    
    
    
    BEGIN_REVERSE_REFLEVEL_LOOP(cgh) {
      
      BEGIN_MGLEVEL_LOOP(cgh) {
	
        do_global_mode = reflevel == 0;
        
        Waypoint ("%*sCurrent time is %g, delta is %g%s", 2*reflevel, "",
                  cgh->cctk_time,
                  cgh->cctk_delta_time / cgh->cctk_timefac,
                  do_global_mode ? "   (global time)" : "");
        
	// Restrict
 	Restrict (cgh);
        
        Waypoint ("%*sScheduling POSTRESTRICT", 2*reflevel, "");
        CCTK_ScheduleTraverse ("POSTRESTRICT", cgh, CallFunction);
	
      } END_MGLEVEL_LOOP;
      
    } END_REVERSE_REFLEVEL_LOOP;
    
    
    
    if (init_3_timelevels) {
      // Call Scott's algorithm for getting two extra timelevels of data
      Initialise3TL (cgh);
    }
    
    
    
    BEGIN_REFLEVEL_LOOP(cgh) {
      
      BEGIN_MGLEVEL_LOOP(cgh) {
	
        do_global_mode = reflevel == 0;
        
        Waypoint ("%*sCurrent time is %g, delta is %g%s", 2*reflevel, "",
                  cgh->cctk_time,
                  cgh->cctk_delta_time / cgh->cctk_timefac,
                  do_global_mode ? "   (global time)" : "");
        
	// Checking
	CalculateChecksums (cgh, allbutcurrenttime);
	
	// Recover
	Waypoint ("%*sScheduling RECOVER_VARIABLES", 2*reflevel, "");
	CCTK_ScheduleTraverse ("CCTK_RECOVER_VARIABLES", cgh, CallFunction);
	Waypoint ("%*sScheduling CPINITIAL", 2*reflevel, "");
	CCTK_ScheduleTraverse ("CCTK_CPINITIAL", cgh, CallFunction);
	
	// Analysis
	Waypoint ("%*sScheduling ANALYSIS", 2*reflevel, "");
	CCTK_ScheduleTraverse ("CCTK_ANALYSIS", cgh, CallFunction);
	
	// Output
	Waypoint ("%*sOutputGH", 2*reflevel, "");
	CCTK_OutputGH (cgh);
      
	// Checking
	CheckChecksums (cgh, allbutcurrenttime);
	
      } END_MGLEVEL_LOOP;
      
    } END_REFLEVEL_LOOP;
    
    Waypoint ("done with Initialise.");
    
    return 0;
  }
  
  
  
  void Initialise3TL (cGH* const cgh)
  {
    DECLARE_CCTK_PARAMETERS;
    
    CCTK_INFO ("Initialising three timelevels");
    
    // Act as if this was the first iteration
    cgh->cctk_iteration = 1;
    
    BEGIN_REFLEVEL_LOOP(cgh) {
      BEGIN_MGLEVEL_LOOP(cgh) {
	
	// Cycle time levels (ignore arrays)
	cout << "3TL rl=" << reflevel << " cycling" << endl;
	CycleTimeLevels (cgh);
	
	// Advance level times
	tt->advance_time (reflevel, mglevel);
        cgh->cctk_time = cctk_initial_time + tt->time (0, reflevel, mglevel) * cgh->cctk_delta_time;
	cout << "3TL rl=" << reflevel << " ml=" << mglevel
	     << " time=" << tt->get_time (reflevel, mglevel)
	     << " time=" << cgh->cctk_time / cgh->cctk_delta_time << endl;
	
	// Evolve forward
	Waypoint ("%*sScheduling PRESTEP", 2*reflevel, "");
	CCTK_ScheduleTraverse ("CCTK_PRESTEP", cgh, CallFunction);
	Waypoint ("%*sScheduling EVOL", 2*reflevel, "");
	CCTK_ScheduleTraverse ("CCTK_EVOL", cgh, CallFunction);
	Waypoint ("%*sScheduling POSTSTEP", 2*reflevel, "");
	CCTK_ScheduleTraverse ("CCTK_POSTSTEP", cgh, CallFunction);
	
	// Flip time levels
	cout << "3TL rl=" << reflevel << " flipping" << endl;
	FlipTimeLevels (cgh);
	
	// Invert level times
	for (int rl=0; rl<hh->reflevels(); ++rl) {
	  tt->set_delta (rl, mglevel, - tt->get_delta (rl, mglevel));
	  tt->advance_time (rl, mglevel);
	  tt->advance_time (rl, mglevel);
	}
	cgh->cctk_delta_time *= -1;
        delta_time *= -1;
        cgh->cctk_time = cctk_initial_time - tt->time (0, reflevel, mglevel) * cgh->cctk_delta_time;
	cout << "3TL rl=" << reflevel << " ml=" << mglevel
	     << " time=" << tt->get_time (reflevel, mglevel)
	     << " time=" << cgh->cctk_time / cgh->cctk_delta_time << endl;
	
	// Evolve backward
	Waypoint ("%*sScheduling PRESTEP", 2*reflevel, "");
	CCTK_ScheduleTraverse ("CCTK_PRESTEP", cgh, CallFunction);
	Waypoint ("%*sScheduling EVOL", 2*reflevel, "");
	CCTK_ScheduleTraverse ("CCTK_EVOL", cgh, CallFunction);
	Waypoint ("%*sScheduling POSTSTEP", 2*reflevel, "");
	CCTK_ScheduleTraverse ("CCTK_POSTSTEP", cgh, CallFunction);
	
	// Flip time levels back
	cout << "3TL rl=" << reflevel << " flipping back" << endl;
	FlipTimeLevels (cgh);
	
	// Invert level times back
	for (int rl=0; rl<hh->reflevels(); ++rl) {
	  tt->set_delta (rl, mglevel, - tt->get_delta (rl, mglevel));
	  tt->advance_time (rl, mglevel);
	  tt->advance_time (rl, mglevel);
	}
	cgh->cctk_delta_time *= -1;
        delta_time *= -1;
        cgh->cctk_time = cctk_initial_time + tt->time (0, reflevel, mglevel) * cgh->cctk_delta_time;
	cout << "3TL rl=" << reflevel << " ml=" << mglevel
	     << " time=" << tt->get_time (reflevel, mglevel)
	     << " time=" << cgh->cctk_time / cgh->cctk_delta_time << endl;
	
      } END_MGLEVEL_LOOP;
    } END_REFLEVEL_LOOP;
    
    cout << "Hourglass structure in place" << endl;
      
    // Act as if this was the second iteration
    cgh->cctk_iteration = 2;
    
    // Evolve each level "backwards" one more timestep
    // Starting with the finest level and proceeding to the coarsest
    BEGIN_REVERSE_REFLEVEL_LOOP(cgh) {
      BEGIN_MGLEVEL_LOOP(cgh) {
	
	// Flip time levels
	cout << "3TL rl=" << reflevel << " flipping" << endl;
	FlipTimeLevels (cgh);
	
	// Invert level times
	for (int rl=0; rl<hh->reflevels(); ++rl) {
	  tt->set_delta (rl, mglevel, - tt->get_delta (rl, mglevel));
	  tt->advance_time (rl, mglevel);
	  tt->advance_time (rl, mglevel);
	}
	cgh->cctk_delta_time *= -1;
        delta_time *= -1;
        cgh->cctk_time = cctk_initial_time - tt->time (0, reflevel, mglevel) * cgh->cctk_delta_time;
	cout << "3TL rl=" << reflevel << " ml=" << mglevel
	     << " time=" << tt->get_time (reflevel, mglevel)
	     << " time=" << cgh->cctk_time / cgh->cctk_delta_time << endl;
	
	// Restrict
	cout << "3TL rl=" << reflevel << " restricting" << endl;
	Restrict (cgh);
        
        Waypoint ("%*sScheduling POSTRESTRICT", 2*reflevel, "");
        CCTK_ScheduleTraverse ("POSTRESTRICT", cgh, CallFunction);
	
	// Cycle time levels
	cout << "3TL rl=" << reflevel << " cycling" << endl;
	CycleTimeLevels (cgh);
	
	// Advance level times
	tt->advance_time (reflevel, mglevel);
        cgh->cctk_time = cctk_initial_time - tt->time (0, reflevel, mglevel) * cgh->cctk_delta_time;
	cout << "3TL rl=" << reflevel << " ml=" << mglevel
	     << " time=" << tt->get_time (reflevel, mglevel)
	     << " time=" << cgh->cctk_time / cgh->cctk_delta_time << endl;
	
	// Evolve backward
	Waypoint ("%*sScheduling PRESTEP", 2*reflevel, "");
	CCTK_ScheduleTraverse ("CCTK_PRESTEP", cgh, CallFunction);
	Waypoint ("%*sScheduling EVOL", 2*reflevel, "");
	CCTK_ScheduleTraverse ("CCTK_EVOL", cgh, CallFunction);
	Waypoint ("%*sScheduling POSTSTEP", 2*reflevel, "");
	CCTK_ScheduleTraverse ("CCTK_POSTSTEP", cgh, CallFunction);
	
	// Flip time levels back
	cout << "3TL rl=" << reflevel << " flipping back" << endl;
	FlipTimeLevels (cgh);
	
	// Invert level times back
	for (int rl=0; rl<hh->reflevels(); ++rl) {
	  tt->set_delta (rl, mglevel, - tt->get_delta (rl, mglevel));
	  tt->advance_time (rl, mglevel);
	  tt->advance_time (rl, mglevel);
	}
	cgh->cctk_delta_time *= -1;
        delta_time *= -1;
        cgh->cctk_time = cctk_initial_time + tt->time (0, reflevel, mglevel) * cgh->cctk_delta_time;
	cout << "3TL rl=" << reflevel << " ml=" << mglevel
	     << " time=" << tt->get_time (reflevel, mglevel)
	     << " time=" << cgh->cctk_time / cgh->cctk_delta_time << endl;
	
      } END_MGLEVEL_LOOP;
    } END_REVERSE_REFLEVEL_LOOP;
    
    // Reset stuff
    cgh->cctk_iteration = 0;
    
    CCTK_INFO ("Finished initialising three timelevels");
  }
  
} // namespace Carpet
