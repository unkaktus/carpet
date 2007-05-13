#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "cctk.h"

#include "defs.hh"
#include "dh.hh"
#include "th.hh"
#include "timestat.hh"

#include "ggf.hh"

using namespace std;
using namespace CarpetLib;



// Constructors
ggf::ggf (const int varindex_, const operator_type transport_operator_,
          th& t_, dh& d_,
          const int prolongation_order_time_,
          const int vectorlength_, const int vectorindex_,
          ggf* const vectorleader_)
  : varindex(varindex_), transport_operator(transport_operator_), t(t_),
    prolongation_order_time(prolongation_order_time_),
    h(d_.h), d(d_),
    storage(h.mglevels()),
    vectorlength(vectorlength_), vectorindex(vectorindex_),
    vectorleader(vectorleader_)
{
  assert (&t.h == &d.h);
  
  assert (vectorlength >= 1);
  assert (vectorindex >= 0 and vectorindex < vectorlength);
  assert ((vectorindex==0 and !vectorleader)
          or (vectorindex!=0 and vectorleader));
  
  timelevels_.resize(d.h.mglevels());
  for (int ml=0; ml<d.h.mglevels(); ++ml) {
    timelevels_.AT(ml).resize(d.h.reflevels(), 0);
  }
  
  d.add(this);
}

// Destructors
ggf::~ggf () {
  d.remove(this);
}

// Comparison
bool ggf::operator== (const ggf& f) const {
  return this == &f;
}



// Modifiers
void ggf::set_timelevels (const int ml, const int rl, const int new_timelevels)
{
  assert (ml>=0 and ml<(int)storage.size());
  assert (rl>=0 and rl<(int)storage.AT(ml).size());
  
  assert (new_timelevels >= 0);
  
  if (new_timelevels < timelevels(ml,rl)) {
    
    for (int c=0; c<(int)storage.AT(ml).AT(rl).size(); ++c) {
      for (int tl=new_timelevels; tl<timelevels(ml,rl); ++tl) {
        delete storage.AT(ml).AT(rl).AT(c).AT(tl);
      }
      storage.AT(ml).AT(rl).AT(c).resize (new_timelevels);
    } // for c
    
  } else if (new_timelevels > timelevels(ml,rl)) {
    
    for (int c=0; c<(int)storage.AT(ml).AT(rl).size(); ++c) {
      storage.AT(ml).AT(rl).AT(c).resize (new_timelevels);
      for (int tl=timelevels(ml,rl); tl<new_timelevels; ++tl) {
        storage.AT(ml).AT(rl).AT(c).AT(tl) = typed_data(tl,rl,c,ml);
        storage.AT(ml).AT(rl).AT(c).AT(tl)->allocate
          (d.boxes.AT(ml).AT(rl).AT(c).exterior, h.processor(rl,c));
      } // for tl
    } // for c
    
  }
  
  timelevels_.AT(ml).AT(rl) = new_timelevels;
}



void ggf::recompose_crop ()
{
  // Free storage that will not be needed
  static Timer timer ("ggf::recompose_crop");
  timer.start ();
  
  for (int ml=0; ml<h.mglevels(); ++ml) {
    for (int rl=h.reflevels(); rl<(int)storage.AT(ml).size(); ++rl) {
      for (int c=0; c<(int)storage.AT(ml).AT(rl).size(); ++c) {
        for (int tl=0; tl<(int)storage.AT(ml).AT(rl).AT(c).size(); ++tl) {
          delete storage.AT(ml).AT(rl).AT(c).AT(tl);
        } // for tl
      } // for c
    } // for rl
    storage.AT(ml).resize(h.reflevels());
  } // for ml
  
  timer.stop (0);
}

void ggf::recompose_allocate (const int rl)
{
  // Retain storage that might be needed
  static Timer timer ("ggf::recompose_allocate");
  timer.start ();
  
  oldstorage.resize(storage.size());
  for (int ml=0; ml<(int)storage.size(); ++ml) {
    oldstorage.AT(ml).resize(storage.AT(ml).size());
    oldstorage.AT(ml).AT(rl) = storage.AT(ml).AT(rl);
    storage.AT(ml).AT(rl).clear();
  }
  
  for (int ml=0; ml<d.h.mglevels(); ++ml) {
    timelevels_.AT(ml).resize(d.h.reflevels(), timelevels_.AT(ml).AT(0));
  }
  
  // Resize structure and allocate storage
  storage.resize(h.mglevels());
  for (int ml=0; ml<h.mglevels(); ++ml) {
    storage.AT(ml).resize(h.reflevels());
    storage.AT(ml).AT(rl).resize(h.components(rl));
    for (int c=0; c<h.components(rl); ++c) {
      storage.AT(ml).AT(rl).AT(c).resize(timelevels(ml,rl));
      for (int tl=0; tl<timelevels(ml,rl); ++tl) {
        storage.AT(ml).AT(rl).AT(c).AT(tl) = typed_data(tl,rl,c,ml);
        storage.AT(ml).AT(rl).AT(c).AT(tl)->allocate
          (d.boxes.AT(ml).AT(rl).AT(c).exterior, h.processor(rl,c));
      } // for tl
    } // for c
  } // for ml
  
  timer.stop (0);
}

void ggf::recompose_fill (comm_state & state, int const rl,
                          bool const do_prolongate)
{
  // Initialise the new storage
  static Timer timer ("ggf::recompose_fill");
  timer.start ();

  for (int ml = 0; ml < h.mglevels(); ++ ml) {
    
    vector <int> tls;
    if (do_prolongate and rl > 0 and transport_operator != op_none) {
      int const numtl = timelevels (ml, rl);
      tls.resize (numtl);
      for (int tl = 0; tl < numtl; ++ tl) {
        tls.AT(tl) = tl;
      }
    }
    
    for (int c = 0; c < h.components (rl); ++c) {
      
      // Initialise from the same level of the old hierarchy, where
      // possible
      if (rl < (int)oldstorage.AT(ml).size()) {
        for (int tl = 0; tl < timelevels (ml, rl); ++tl) {
          transfer_from (state,
                         tl, rl, c, ml,
                         & dh::dboxes::fast_old2new_sync_recv,
                         & dh::dboxes::fast_old2new_sync_send,
                         tl, rl, ml,
                         & oldstorage);
        } // for tl
      } // if rl
      
      if (do_prolongate) {
        // Initialise from a coarser level of the new hierarchy, where
        // possible
        if (rl > 0) {
          if (transport_operator != op_none) {
            for (int tl = 0; tl < timelevels (ml, rl); ++tl) {
              transfer_from (state,
                             tl, rl, c, ml,
                             & dh::dboxes::fast_old2new_ref_prol_recv,
                             & dh::dboxes::fast_old2new_ref_prol_send,
                             tls, rl - 1, ml,
                             t.time (tl, rl, ml));
            } // for tl
          } // if transport_operator
        } // if rl
      } // if do_prolongate
      
      // Note that work need not be empty here; in this case, not
      // everything could be initialised.  This is okay on outer
      // boundaries.
      // TODO: check this.
      
    } // for c
  } // for ml
  
  timer.stop (0);
}

void ggf::recompose_free_old (const int rl)
{
  // Delete old storage
  static Timer timer ("dh::recompose");
  timer.start ();

  for (int ml=0; ml<(int)oldstorage.size(); ++ml) {
    for (int c=0; c<(int)oldstorage.AT(ml).AT(rl).size(); ++c) {
      for (int tl=0; tl<timelevels(ml,rl); ++tl) {
        delete oldstorage.AT(ml).AT(rl).AT(c).AT(tl);
      } // for tl
    } // for c
    oldstorage.AT(ml).AT(rl).clear();
  } // for ml
  
  timer.stop (0);
}



// Cycle the time levels by rotating the data sets
void ggf::cycle (int const rl, int const c, int const ml) {
  assert (rl>=0 and rl<h.reflevels());
  assert (c>=0 and c<h.components(rl));
  assert (ml>=0 and ml<h.mglevels());
  int const ntl = timelevels(ml,rl);
  assert (ntl > 0);
  fdata & fdatas = storage.AT(ml).AT(rl).AT(c);
  gdata * const tmpdata = fdatas.AT(ntl-1);
  for (int tl=ntl-1; tl>0; --tl) {
    fdatas.AT(tl) = fdatas.AT(tl-1);
  }
  fdatas.AT(0) = tmpdata;
}

// Flip the time levels by exchanging the data sets
void ggf::flip (int rl, int c, int ml) {
  assert (rl>=0 and rl<h.reflevels());
  assert (c>=0 and c<h.components(rl));
  assert (ml>=0 and ml<h.mglevels());
  for (int tl=0; tl<(timelevels(ml,rl)-1)/2; ++tl) {
    const int tl1 =                  tl;
    const int tl2 = timelevels(ml,rl)-1 - tl;
    assert (tl1 < tl2);
    gdata* tmpdata = storage.AT(ml).AT(rl).AT(c).AT(tl1);
    storage.AT(ml).AT(rl).AT(c).AT(tl1) = storage.AT(ml).AT(rl).AT(c).AT(tl2);
    storage.AT(ml).AT(rl).AT(c).AT(tl2) = tmpdata;
  }
}



#if 0
// Copy a component from the next time level
void ggf::copy (comm_state& state, int tl, int rl, int c, int ml)
{
  // Copy
  static Timer timer ("copy");
  timer.start ();
  transfer_from (state,
                 tl  ,rl,c,ml, &dh::dboxes::exterior,
                 tl+1,rl,  ml);
  timer.stop (0);
}
#endif



// Synchronise the boundaries a component
void
ggf::
sync (comm_state & state,
      int const tl, int const rl, int const c, int const ml)
{
  // Copy
  static Timer timer ("sync");
  timer.start ();
  transfer_from (state,
                 tl,rl,c,ml,
                 & dh::dboxes::fast_sync_recv,
                 & dh::dboxes::fast_sync_send,
                 tl,rl,  ml);
  timer.stop (0);
}



// Prolongate the boundaries of a component
void
ggf::
ref_bnd_prolongate (comm_state & state, 
                    int const tl, int const rl, int const c, int const ml,
                    CCTK_REAL const time)
{
  // Interpolate
  assert (rl>=1);
  if (transport_operator == op_none) return;
  vector<int> tl2s;
  static Timer timer ("ref_bnd_prolongate");
  timer.start ();
  if (transport_operator != op_copy) {
    // Interpolation in time
    if (not (timelevels(ml,rl) >= prolongation_order_time+1)) {
      char * const fullname = CCTK_FullName (varindex);
      CCTK_VWarn (0, __LINE__, __FILE__, CCTK_THORNSTRING,
                  "The variable \"%s\" has only %d active time levels, which is not enough for boundary prolongation of order %d",
                  fullname ? fullname : "<unknown variable>",
                  timelevels(ml,rl), prolongation_order_time);
      free (fullname);
    }
    assert (timelevels(ml,rl) >= prolongation_order_time+1);
    tl2s.resize(prolongation_order_time+1);
    for (int i=0; i<=prolongation_order_time; ++i) tl2s.AT(i) = i;
  } else {
    assert (timelevels(ml,rl) >= 1);
    tl2s.resize(1);
    tl2s.AT(0) = 0;
  }
  transfer_from (state,
                 tl  ,rl  ,c,ml,
                 & dh::dboxes::fast_ref_bnd_prol_recv,
                 & dh::dboxes::fast_ref_bnd_prol_send,
                 tl2s,rl-1,  ml,
                 time);
  timer.stop (0);
}



// Restrict a multigrid level
void
ggf::
mg_restrict (comm_state & state,
             int const tl, int const rl, int const c, int const ml,
             CCTK_REAL const time)
{
  static Timer timer ("mg_restrict");
  timer.start ();
  // Require same times
  static_assert (abs(0.1) > 0, "Function CarpetLib::abs has wrong signature");
  assert (abs(t.get_time(rl,ml) - t.get_time(rl,ml-1))
	  <= 1.0e-8 * abs(t.get_time(rl,ml)));
  vector<int> const tl2s(1,tl);
  transfer_from (state,
                 tl  ,rl,c,ml,
                 & dh::dboxes::fast_mg_rest_recv,
                 & dh::dboxes::fast_mg_rest_send,
                 tl2s,rl,  ml-1,
                 time);
  timer.stop (0);
}



// Prolongate a multigrid level
void
ggf::
mg_prolongate (comm_state & state,
               int const tl, int const rl, int const c, int const ml,
               CCTK_REAL const time)
{
  static Timer timer ("mg_prolongate");
  timer.start ();
  // Require same times
  static_assert (abs(0.1) > 0, "Function CarpetLib::abs has wrong signature");
  assert (abs(t.get_time(rl,ml) - t.get_time(rl,ml+1))
	  <= 1.0e-8 * abs(t.get_time(rl,ml)));
  vector<int> const tl2s(1,tl);
  transfer_from (state,
                 tl  ,rl,c,ml,
                 & dh::dboxes::fast_mg_prol_recv,
                 & dh::dboxes::fast_mg_prol_send,
                 tl2s,rl,  ml+1,
                 time);
  timer.stop (0);
}



// Restrict a refinement level
void
ggf::
ref_restrict (comm_state & state,
              int const tl, int const rl, int const c, int const ml,
              CCTK_REAL const time)
{
  // Require same times
  static_assert (abs(0.1) > 0, "Function CarpetLib::abs has wrong signature");
  assert (abs(t.get_time(rl,ml) - t.get_time(rl+1,ml))
	  <= 1.0e-8 * abs(t.get_time(rl,ml)));
  if (transport_operator == op_none) return;
  static Timer timer ("ref_restrict");
  timer.start ();
  vector<int> const tl2s(1,tl);
  transfer_from (state,
                 tl  ,rl  ,c,ml,
                 & dh::dboxes::fast_ref_rest_recv,
                 & dh::dboxes::fast_ref_rest_send,
                 tl2s,rl+1,  ml,
                 time);
  timer.stop (0);
}



// Prolongate a refinement level
void
ggf::
ref_prolongate (comm_state & state,
                int const tl, int const rl, int const c, int const ml,
                CCTK_REAL const time)
{
  assert (rl>=1);
  if (transport_operator == op_none) return;
  static Timer timer ("ref_prolongate");
  timer.start ();
  vector<int> tl2s;
  // Interpolation in time
  assert (timelevels(ml,rl) >= prolongation_order_time+1);
  tl2s.resize(prolongation_order_time+1);
  for (int i=0; i<=prolongation_order_time; ++i) tl2s.AT(i) = i;
  transfer_from (state,
                 tl  ,rl  ,c,ml,
                 & dh::dboxes::fast_ref_prol_recv,
                 & dh::dboxes::fast_ref_prol_send,
                 tl2s,rl-1,  ml,
                 time);
  timer.stop (0);
}



// Transfer regions
void
ggf::
transfer_from (comm_state & state,
               int const tl1, int const rl1, int const c1, int const ml1,
               pvect const dh::dboxes::* recvs,
               pvect const dh::dboxes::* sends,
               vector<int> const & tl2s, int const rl2, int const ml2,
               CCTK_REAL const & time,
               mdata * const srcstorage_)
{
  assert (rl1>=0 and rl1<h.reflevels());
  assert (c1>=0 and c1<h.components(rl1));
  assert (ml1>=0 and ml1<h.mglevels());
  assert (tl1>=0 and tl1<timelevels(ml1,rl1));
  
  pvect const & precvs = d.boxes.AT(ml1).AT(rl1).AT(c1).*recvs;
  pvect const & psends = d.boxes.AT(ml1).AT(rl1).AT(c1).*sends;
  assert (precvs.size() == psends.size());
  
  mdata & srcstorage = srcstorage_ ? * srcstorage_ : storage;
  
  if (not precvs.empty()) {
    // Check only when *recvs is not empty
    assert (           ml2<(int)srcstorage.size());
    assert (rl2>=0 and rl2<(int)srcstorage.AT(ml2).size());
    for (size_t i = 0; i < tl2s.size(); ++ i) {
      int const tl2 = tl2s.AT(i);
      assert (tl2>=0 and tl2<(int)srcstorage.AT(ml2).AT(rl2).AT(0).size());
    }
  }
  
  static Timer total ("transfer_from");
  total.start ();
  
  // Interpolation orders
  int const pos =
    transport_operator == op_copy ? 0 : d.prolongation_order_space;
  int const pot =
    transport_operator == op_copy ? 0 : prolongation_order_time;
  
  // Source and destination data
  gdata * const dst = storage.AT(ml1).AT(rl1).AT(c1).AT(tl1);
  cdata const & srcs = srcstorage.AT(ml2).AT(rl2);
  vector<const gdata*> gsrcs(tl2s.size());
  
  // Set up source times
  vector<CCTK_REAL> times(tl2s.size());
  for (size_t i=0; i<tl2s.size(); ++i) {
    assert (tl2s.AT(i)>=0 and tl2s.AT(i)<timelevels(ml2,rl2));
    for (size_t j=0; j<i; ++j) {
      assert (tl2s.AT(i) != tl2s.AT(j));
    }
    times.AT(i) = t.time(tl2s.AT(i),rl2,ml2);
  }
  
  // Walk all regions
  for (pvect::const_iterator iprecv=precvs.begin(), ipsend=psends.begin();
       iprecv!=precvs.end(); ++ iprecv, ++ ipsend)
  {
    pseudoregion const & precv = * iprecv;
    pseudoregion const & psend = * ipsend;
    ibbox const & recv = precv.extent;
    ibbox const & send = psend.extent;
    int const c2 = precv.processor;
    assert (psend.processor == c2);
    for (int i=0; i<(int)gsrcs.size(); ++i) {
      gsrcs.AT(i) = srcs.AT(c2).AT(tl2s.AT(i));
    }
    dst->transfer_from (state, gsrcs, times, recv, send, time, pos, pot);
  }
  
  total.stop (0);
}
