#ifndef GGF_HH
#define GGF_HH

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "cctk.h"

#include "defs.hh"
#include "dh.hh"
#include "gdata.hh"
#include "gh.hh"
#include "th.hh"

using namespace std;



// Forward declaration
class ggf;

// Output
ostream& operator<< (ostream& os, const ggf& f);



// A generic grid function without type information
class ggf {

  // Types
  typedef list<ibbox>    iblist;
  typedef vector<iblist> iblistvect;
  
  typedef vector <pseudoregion> pvect;
  
  typedef gdata*        tdata;  // data ...
  typedef vector<tdata> fdata;  // ... for each time level
  typedef vector<fdata> cdata;  // ... for each component
  typedef vector<cdata> rdata;  // ... for each refinement level
  typedef vector<rdata> mdata;  // ... for each multigrid level
  
public:				// should be readonly
  
  // Fields
  const int varindex;           // Cactus variable index
  const operator_type transport_operator;
  
  const th &t;                  // time hierarchy
  const int prolongation_order_time; // order of temporal prolongation operator
  
  const gh &h;                  // grid hierarchy
  dh &d;			// data hierarchy

protected:
  vector<vector<int> > timelevels_; // time levels [ml][rl]

  mdata storage;		// storage
  
public:
  const int vectorlength;       // vector length
  const int vectorindex;        // index of *this
  const ggf* vectorleader;      // first vector element
  
private:
  mdata oldstorage;             // temporary storage
  
public:

  // Constructors
  ggf (const int varindex, const operator_type transport_operator,
       th& t, dh& d,
       const int prolongation_order_time,
       const int vectorlength, const int vectorindex,
       ggf* const vectorleader);

  // Destructors
  virtual ~ggf ();

  // Comparison
  bool operator== (const ggf& f) const;
  
  // Querying
  int timelevels (int const ml, int const rl) const
  {
    return timelevels_.AT(ml).AT(rl);
  }
  
  
  
  // Modifiers
  void set_timelevels (int ml, int rl, int new_timelevels);

  void recompose_crop ();
  void recompose_allocate (int rl);
  void recompose_fill (comm_state& state, int rl, bool do_prolongate);
  void recompose_free_old (int rl);

  // Cycle the time levels by rotating the data sets
  void cycle (int rl, int c, int ml);
  
  // Flip the time levels by exchanging the data sets
  void flip (int rl, int c, int ml);

  // Fill all time levels from the current time level
  void fill (int rl, int c, int ml);

  // The grid boundaries have to be updated after calling mg_restrict,
  // mg_prolongate, ref_restrict, or ref_prolongate.

  // "Updating" means here that the boundaries have to be
  // synchronised.  They don't need to be prolongated.

  // Synchronise the boundaries of a component
  void sync (comm_state& state, int tl, int rl, int c, int ml);

  // Prolongate the boundaries of a component
  void ref_bnd_prolongate (comm_state& state,
                           int tl, int rl, int c, int ml, CCTK_REAL time);

  // Restrict a multigrid level
  void mg_restrict (comm_state& state,
                    int tl, int rl, int c, int ml, CCTK_REAL time);

  // Prolongate a multigrid level
  void mg_prolongate (comm_state& state,
                      int tl, int rl, int c, int ml, CCTK_REAL time);

  // Restrict a refinement level
  void ref_restrict (comm_state& state,
                     int tl, int rl, int c, int ml, CCTK_REAL time);

  // Prolongate a refinement level
  void ref_prolongate (comm_state& state,
                       int tl, int rl, int c, int ml, CCTK_REAL time);
  
  
  
  // Helpers
  
protected:
  
  virtual gdata* typed_data (int tl, int rl, int c, int ml) = 0;
  
  
  
protected:
  
  // Transfer regions
  void
  transfer_from (comm_state & state,
                 int tl1, int rl1, int c1, int ml1,
                 pvect const dh::dboxes::* recvs,
                 pvect const dh::dboxes::* sends,
                 vector<int> const & tl2s, int rl2, int ml2,
                 CCTK_REAL const & time,
                 mdata * srcstorage = 0);
  
  void
  transfer_from (comm_state & state,
                 int tl1, int rl1, int c1, int ml1,
                 pvect const dh::dboxes::* recvs,
                 pvect const dh::dboxes::* sends,
                 int tl2, int rl2, int ml2,
                 mdata * srcstorage = 0)
  {
    vector <int> tl2s(1);
    tl2s.AT(0) = tl2;
    CCTK_REAL const time = t.time (tl2,rl2,ml2);
    transfer_from (state,
                   tl1, rl1, c1, ml1,
                   recvs, sends,
                   tl2s, rl2, ml2,
                   time,
                   srcstorage);
  }



public:
  
  // Access to the data
  virtual const gdata* operator() (int tl, int rl, int c, int ml) const = 0;
  
  virtual gdata* operator() (int tl, int rl, int c, int ml) = 0;
  
  
  
  // Output
  virtual ostream& output (ostream& os) const = 0;

private:
  ggf ();                       // canonical default construtor
  ggf (const ggf &);            // canonical copy construtor
  ggf & operator= (const ggf &); // canonical copy

};



template<int D>
inline ostream& operator<< (ostream& os, const ggf& f)
{
  return f.output(os);
}



#endif // GGF_HH
