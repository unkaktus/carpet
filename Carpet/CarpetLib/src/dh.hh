#ifndef DH_HH
#define DH_HH

#include <cassert>
#include <iostream>
#include <list>
#include <string>
#include <vector>

#include "bbox.hh"
#include "bboxset.hh"
#include "defs.hh"
#include "gh.hh"
#include "vect.hh"

using namespace std;



// Forward declaration
class ggf;
class dh;

// Output
ostream& operator<< (ostream& os, const dh& d);



// A data hierarchy (grid hierarchy plus ghost zones)
class dh {
  
  // Types
public:
  typedef list<ibbox>    iblist;
  typedef vector<iblist> iblistvect; // vector of lists


  // in here, the term "boundary" means both ghost zones and
  // refinement boundaries, but does not refer to outer (physical)
  // boundaries.
  
  // ghost zones, refinement boundaries, and outer boundaries are not
  // used as sources for synchronisation.  this design choice might
  // not be good.
  
  struct dboxes {
    ibbox exterior;             // whole region (including boundaries)
    bbvect is_interproc;        // the whole boundary is an
                                // interprocessor boundary
    
    ibbox interior;             // interior (without boundaries)
    iblist send_mg_fine;
    iblist send_mg_coarse;
    iblist recv_mg_fine;
    iblist recv_mg_coarse;
    iblistvect send_ref_fine;
    iblistvect send_ref_coarse;
    iblistvect recv_ref_fine;
    iblistvect recv_ref_coarse;
    iblistvect send_sync;       // send while syncing
    iblistvect send_ref_bnd_fine; // sent to finer grids
    
    ibset boundaries;           // boundaries
    iblistvect recv_sync;       // received while syncing
    iblistvect recv_ref_bnd_coarse; // received from coarser grids
    ibset sync_not;             // not received while syncing (outer
                                // boundary of that level)
    ibset recv_not;             // not received while syncing or
                                // prolongating (globally outer
                                // boundary)
  };
  
private:
  
  struct dbases {
    ibbox exterior;             // whole region (including boundaries)
    ibbox interior;             // interior (without boundaries)
    ibset boundaries;           // boundaries
  };
  
  typedef vector<dboxes> cboxes; // ... for each component
  typedef vector<cboxes> rboxes; // ... for each refinement level
  typedef vector<rboxes> mboxes; // ... for each multigrid level
  
  typedef vector<dbases> rbases; // ... for each refinement level
  typedef vector<rbases> mbases; // ... for each multigrid level
 
  void allocate_bboxes ();

  // generic member function taking a dboxes, a refinement level, a
  // component, and a multigrid level
  typedef void (dh::*boxesop) (dboxes &, int rl, int c, int ml); 
  void foreach_reflevel_component_mglevel (boxesop op);

  // these all of form 'boxesop'
  void setup_sync_and_refine_boxes (dboxes & b, int rl, int c, int ml);
  void setup_sync_boxes (dboxes & b, int rl, int c, int ml);
  void setup_multigrid_boxes (dboxes & b, int rl, int c, int ml);
  void setup_refinement_prolongation_boxes (dboxes & b, int rl, int c, int ml);
  void setup_refinement_boundary_prolongation_boxes (dboxes & b, int rl, int c, int ml);
  void setup_refinement_restriction_boxes (dboxes & b, int rl, int c, int ml);
  void trim_unsynced_boundaries (dboxes & b, int rl, int c, int ml);
  void do_output_bboxes (dboxes & b, int rl, int c, int ml);
  void check_bboxes (dboxes & b, int rl, int c, int ml);

  void calculate_bases (); 
  void output_bases (); 
  void save_time (bool do_prolongate); 
  void save_memory (bool do_prolongate); 

public:                         // should be readonly
  
  // Fields
  gh& h;                        // hierarchy
  i2vect ghosts;                // ghost zones
  
  int prolongation_order_space; // order of spatial prolongation operator
  int inner_buffer_width;       // buffer inside refined grids
  i2vect buffers;               // buffer outside refined grids
  
  mboxes boxes;                 // grid hierarchy
  mbases bases;                 // bounding boxes around the grid
                                // hierarchy
  
  list<ggf*> gfs;               // list of all grid functions
  
public:
  
  // Constructors
  dh (gh& h, const ivect& lghosts, const ivect& ughosts,
      int prolongation_order_space, int inner_buffer_width,
      const ivect& lbuffers, const ivect& ubuffers);
  
  // Destructors
  virtual ~dh ();
  
  // Helpers
  int prolongation_stencil_size () const;
  
  // Modifiers
  void regrid ();
  void recompose (const int rl, const bool do_prolongate);
  
  // Grid function management
  void add (ggf* f);
  void remove (ggf* f);
  
  // Output
  virtual void output (ostream& os) const;
};



inline ostream& operator<< (ostream& os, const dh& d)
{
  d.output (os);
  return os;
}



#endif // DH_HH
