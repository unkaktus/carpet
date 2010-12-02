#include <cctk.h>

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "CarpetTimers.hh"

#include "defs.hh"
#include "dh.hh"
#include "th.hh"
#include "timestat.hh"

#include "ggf.hh"

using namespace std;
using namespace CarpetLib;



list<ggf*> ggf::allggf;



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
  // assert (&t.h == &d.h);
  
  assert (vectorlength >= 1);
  assert (vectorindex >= 0 and vectorindex < vectorlength);
  assert ((vectorindex==0 and not vectorleader)
          or (vectorindex!=0 and vectorleader));
  
  timelevels_.resize(d.h.mglevels());
  for (int ml=0; ml<d.h.mglevels(); ++ml) {
    timelevels_.AT(ml).resize(d.h.reflevels(), 0);
  }
  
  allggfi = allggf.insert(allggf.end(), this);
  
  dh_handle = d.add(this);
}

// Destructors
ggf::~ggf () {
  d.erase(dh_handle);
  allggf.erase(allggfi);
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
    
    for (int lc=0; lc<h.local_components(rl); ++ lc) {
      for (int tl=new_timelevels; tl<timelevels(ml,rl); ++tl) {
        delete storage.AT(ml).AT(rl).AT(lc).AT(tl);
      }
      storage.AT(ml).AT(rl).AT(lc).resize (new_timelevels);
    } // for lc
    
  } else if (new_timelevels > timelevels(ml,rl)) {
    
    for (int lc=0; lc<h.local_components(rl); ++ lc) {
      int const c = h.get_component(rl,lc);
      storage.AT(ml).AT(rl).AT(lc).resize (new_timelevels);
      for (int tl=timelevels(ml,rl); tl<new_timelevels; ++tl) {
        storage.AT(ml).AT(rl).AT(lc).AT(tl) = typed_data(tl,rl,lc,ml);
        storage.AT(ml).AT(rl).AT(lc).AT(tl)->allocate
          (d.light_boxes.AT(ml).AT(rl).AT(c).exterior, dist::rank());
      } // for tl
    } // for lc
    
  }
  
  timelevels_.AT(ml).AT(rl) = new_timelevels;
}



void ggf::recompose_crop ()
{
  // Free storage that will not be needed
  static Carpet::Timer timer ("CarpetLib::ggf::recompose_crop");
  timer.start ();
  
  for (int ml=0; ml<h.mglevels(); ++ml) {
    for (int rl=h.reflevels(); rl<(int)storage.AT(ml).size(); ++rl) {
      for (int lc=0; lc<(int)storage.AT(ml).AT(rl).size(); ++lc) {
        for (int tl=0; tl<(int)storage.AT(ml).AT(rl).AT(lc).size(); ++tl) {
          delete storage.AT(ml).AT(rl).AT(lc).AT(tl);
        } // for tl
      } // for lc
    } // for rl
    storage.AT(ml).resize(h.reflevels());
  } // for ml
  
  timer.stop ();
}

void ggf::recompose_allocate (const int rl)
{
  // Retain storage that might be needed
  static Carpet::Timer timer ("CarpetLib::ggf::recompose_allocate");
  timer.start ();
  
  oldstorage.resize(storage.size());
  for (int ml=0; ml<(int)storage.size(); ++ml) {
    oldstorage.AT(ml).resize(storage.AT(ml).size());
#if 0
    oldstorage.AT(ml).AT(rl) = storage.AT(ml).AT(rl);
    storage.AT(ml).AT(rl).clear();
#else
    oldstorage.AT(ml).AT(rl).clear();
    swap (storage.AT(ml).AT(rl), oldstorage.AT(ml).AT(rl));
#endif
  }
  
  for (int ml=0; ml<d.h.mglevels(); ++ml) {
    timelevels_.AT(ml).resize(d.h.reflevels(), timelevels_.AT(ml).AT(0));
  }
  
  // Resize structure and allocate storage
  storage.resize(h.mglevels());
  for (int ml=0; ml<h.mglevels(); ++ml) {
    storage.AT(ml).resize(h.reflevels());
    storage.AT(ml).AT(rl).resize(h.local_components(rl));
    for (int lc=0; lc<h.local_components(rl); ++ lc) {
      int const c = h.get_component(rl,lc);
      storage.AT(ml).AT(rl).AT(lc).resize(timelevels(ml,rl));
      for (int tl=0; tl<timelevels(ml,rl); ++tl) {
        storage.AT(ml).AT(rl).AT(lc).AT(tl) = typed_data(tl,rl,lc,ml);
        storage.AT(ml).AT(rl).AT(lc).AT(tl)->allocate
          (d.light_boxes.AT(ml).AT(rl).AT(c).exterior, dist::rank());
      } // for tl
    } // for lc
  } // for ml
  
  timer.stop ();
}

void ggf::recompose_fill (comm_state & state, int const rl,
                          bool const do_prolongate)
{
  // Initialise the new storage
  static Carpet::Timer timer ("CarpetLib::ggf::recompose_fill");
  timer.start ();
  
  for (int ml = 0; ml < h.mglevels(); ++ ml) {
    
    assert (d.fast_boxes.AT(ml).AT(rl).do_init);
    
    // Initialise from the same level of the old hierarchy, where
    // possible
    if (rl < (int)oldstorage.AT(ml).size()) {
      for (int tl = 0; tl < timelevels (ml, rl); ++tl) {
        transfer_from_all (state,
                           tl, rl, ml,
                           & dh::fast_dboxes::fast_old2new_sync_sendrecv,
                           tl, rl, ml,
                           & oldstorage);
      } // for tl
    } // if rl
    
    if (do_prolongate) {
      // Initialise from a coarser level of the new hierarchy, where
      // possible
      if (rl > 0) {
        if (transport_operator != op_none and transport_operator != op_sync and
            transport_operator != op_restrict)
        {
          int const numtl = timelevels (ml, rl);
          vector <int> tls (numtl);
          for (int tl = 0; tl < numtl; ++ tl) {
            tls.AT(tl) = tl;
          }

          for (int tl = 0; tl < timelevels (ml, rl); ++tl) {
            transfer_from_all (state,
                               tl, rl, ml,
                               & dh::fast_dboxes::fast_old2new_ref_prol_sendrecv,
                               tls, rl - 1, ml,
                               t.get_time (ml, rl, tl));
          } // for tl
        } // if transport_operator
      } // if rl
    } // if do_prolongate
    
  } // for ml
  
  timer.stop ();
}

void ggf::recompose_free_old (const int rl)
{
  // Delete old storage
  static Carpet::Timer timer ("dh::recompose_free_old");
  timer.start ();

  for (int ml=0; ml<(int)oldstorage.size(); ++ml) {
    for (int lc=0; lc<(int)oldstorage.AT(ml).AT(rl).size(); ++lc) {
      for (int tl=0; tl<(int)oldstorage.AT(ml).AT(rl).AT(lc).size(); ++tl) {
        delete oldstorage.AT(ml).AT(rl).AT(lc).AT(tl);
      } // for tl
    } // for lc
    oldstorage.AT(ml).AT(rl).clear();
  } // for ml
  
  timer.stop ();
}

void ggf::recompose_free (const int rl)
{
  // Delete old storage
  static Carpet::Timer timer ("dh::recompose_free");
  timer.start ();
  
  for (int ml=0; ml<(int)storage.size(); ++ml) {
    for (int lc=0; lc<h.local_components(rl); ++ lc) {
      for (int tl=0; tl<timelevels(ml,rl); ++tl) {
        delete storage.AT(ml).AT(rl).AT(lc).AT(tl);
      } // for tl
    } // for lc
    storage.AT(ml).AT(rl).clear();
  } // for ml
  
  timer.stop ();
}



// Cycle the time levels by rotating the data sets
void ggf::cycle_all (int const rl, int const ml) {
  assert (rl>=0 and rl<h.reflevels());
  assert (ml>=0 and ml<h.mglevels());
  int const ntl = timelevels(ml,rl);
  assert (ntl > 0);
  for (int lc=0; lc<(int)storage.AT(ml).AT(rl).size(); ++lc) {
    fdata & fdatas = storage.AT(ml).AT(rl).AT(lc);
    gdata * const tmpdata = fdatas.AT(ntl-1);
    for (int tl=ntl-1; tl>0; --tl) {
      fdatas.AT(tl) = fdatas.AT(tl-1);
    }
    fdatas.AT(0) = tmpdata;
  }
}

// Uncycle the time levels by rotating the data sets
void ggf::uncycle_all (int const rl, int const ml) {
  assert (rl>=0 and rl<h.reflevels());
  assert (ml>=0 and ml<h.mglevels());
  int const ntl = timelevels(ml,rl);
  assert (ntl > 0);
  for (int lc=0; lc<(int)storage.AT(ml).AT(rl).size(); ++lc) {
    fdata & fdatas = storage.AT(ml).AT(rl).AT(lc);
    gdata * const tmpdata = fdatas.AT(0);
    for (int tl=0; tl<ntl-1; ++tl) {
      fdatas.AT(tl) = fdatas.AT(tl+1);
    }
    fdatas.AT(ntl-1) = tmpdata;
  }
}

// Flip the time levels by exchanging the data sets
void ggf::flip_all (int const rl, int const ml) {
  assert (rl>=0 and rl<h.reflevels());
  assert (ml>=0 and ml<h.mglevels());
  int const ntl = timelevels(ml,rl);
  assert (ntl > 0);
  for (int lc=0; lc<(int)storage.AT(ml).AT(rl).size(); ++lc) {
    fdata & fdatas = storage.AT(ml).AT(rl).AT(lc);
    for (int tl=1; tl<(ntl+1)/2; ++tl) {
      const int tl1 =       tl;
      const int tl2 = ntl - tl;
      assert (tl1 < tl2);
      gdata * const tmpdata = fdatas.AT(tl1);
      fdatas.AT(tl1) = fdatas.AT(tl2);
      fdatas.AT(tl2) = tmpdata;
    }
  }
}



// Fill all time levels from the current time level
void ggf::fill_all (int const rl, int const ml) {
  assert (rl>=0 and rl<h.reflevels());
  assert (ml>=0 and ml<h.mglevels());
  int const ntl = timelevels(ml,rl);
  assert (ntl > 0);
  for (int lc=0; lc<(int)storage.AT(ml).AT(rl).size(); ++lc) {
    fdata const & fdatas = storage.AT(ml).AT(rl).AT(lc);
    void const * const srcptr = fdatas.AT(0)->storage();
    size_t const size = fdatas.AT(0)->size() * fdatas.AT(0)->elementsize();
    for (int tl=1; tl<ntl; ++tl) {
      void * const dstptr = fdatas.AT(tl)->storage();
      memcpy (dstptr, srcptr, size);
    }
  }
}



// Synchronise the boundaries of all components
void
ggf::
sync_all (comm_state & state,
          int const tl, int const rl, int const ml)
{
  if (transport_operator == op_none) return;
  // Copy
  static Timer timer ("sync_all");
  timer.start ();
  transfer_from_all (state,
                     tl,rl,ml,
                     & dh::fast_dboxes::fast_sync_sendrecv,
                     tl,rl,ml);
  timer.stop (0);
}



// Accumulate the boundaries of all components
void
ggf::
accumulate_all (comm_state & state,
                int const tl, int const rl, int const ml)
{
  assert (transport_operator == op_accumulate);
  // Accumulate
  static Timer timer ("accumulate_all");
  timer.start ();
  transfer_from_all (state,
                     tl,rl,ml,
                     & dh::fast_dboxes::fast_sync_sendrecv,
                     tl,rl,ml,
                     NULL, true);
  timer.stop (0);
}



// Prolongate the boundaries of all components
void
ggf::
ref_bnd_prolongate_all (comm_state & state, 
                        int const tl, int const rl, int const ml,
                        CCTK_REAL const time)
{
  // Interpolate
  assert (rl>=1);
  if (transport_operator == op_none or transport_operator == op_sync or
      transport_operator == op_restrict)
    return;
  vector<int> tl2s;
  static Timer timer ("ref_bnd_prolongate_all");
  timer.start ();
  if (transport_operator != op_copy and transport_operator != op_accumulate) {
    // Interpolation in time
    if (not (timelevels(ml,rl) >= prolongation_order_time+1)) {
      char * const fullname = CCTK_FullName (varindex);
      CCTK_VWarn (CCTK_WARN_ABORT, __LINE__, __FILE__, CCTK_THORNSTRING,
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
  transfer_from_all (state,
                     tl  ,rl  ,ml,
                     & dh::fast_dboxes::fast_ref_bnd_prol_sendrecv,
                     tl2s,rl-1,ml,
                     time);
  timer.stop (0);
}



// Restrict a multigrid level
void
ggf::
mg_restrict_all (comm_state & state,
                 int const tl, int const rl, int const ml,
                 CCTK_REAL const time)
{
  static Timer timer ("mg_restrict_all");
  timer.start ();
  // Require same times
  static_assert (abs(0.1) > 0, "Function CarpetLib::abs has wrong signature");
  assert (abs(t.get_time(ml,rl,0) - t.get_time(ml-1,rl,0))
	  <= 1.0e-8 * (1.0 + abs(t.get_time(ml,rl,0))));
  vector<int> const tl2s(1,tl);
  transfer_from_all (state,
                     tl  ,rl,ml,
                     & dh::fast_dboxes::fast_mg_rest_sendrecv,
                     tl2s,rl,ml-1,
                     time);
  timer.stop (0);
}



// Prolongate a multigrid level
void
ggf::
mg_prolongate_all (comm_state & state,
                   int const tl, int const rl, int const ml,
                   CCTK_REAL const time)
{
  static Timer timer ("mg_prolongate_all");
  timer.start ();
  // Require same times
  static_assert (abs(0.1) > 0, "Function CarpetLib::abs has wrong signature");
  assert (abs(t.get_time(ml,rl,0) - t.get_time(ml+1,rl,0))
	  <= 1.0e-8 * (1.0 + abs(t.get_time(ml,rl,0))));
  vector<int> const tl2s(1,tl);
  transfer_from_all (state,
                     tl  ,rl,ml,
                     & dh::fast_dboxes::fast_mg_prol_sendrecv,
                     tl2s,rl,ml+1,
                     time);
  timer.stop (0);
}



// Restrict a refinement level
void
ggf::
ref_restrict_all (comm_state & state,
                  int const tl, int const rl, int const ml)
{
  if (transport_operator == op_none or transport_operator == op_sync) return;
  static Timer timer ("ref_restrict_all");
  timer.start ();
  // Require same times
  static_assert (abs(0.1) > 0, "Function CarpetLib::abs has wrong signature");
  assert (abs(t.get_time(ml,rl,tl) - t.get_time(ml,rl+1,tl)) <=
          1.0e-8 * (1.0 + abs(t.get_time(ml,rl,tl))));
  transfer_from_all (state,
                     tl,rl  ,ml,
                     & dh::fast_dboxes::fast_ref_rest_sendrecv,
                     tl,rl+1,ml);
  timer.stop (0);
}



// Prolongate a refinement level
void
ggf::
ref_prolongate_all (comm_state & state,
                    int const tl, int const rl, int const ml,
                    CCTK_REAL const time)
{
  assert (rl>=1);
  if (transport_operator == op_none or transport_operator == op_sync or
      transport_operator == op_restrict)
    return;
  static Timer timer ("ref_prolongate_all");
  timer.start ();
  vector<int> tl2s;
  // Interpolation in time
  assert (timelevels(ml,rl) >= prolongation_order_time+1);
  tl2s.resize(prolongation_order_time+1);
  for (int i=0; i<=prolongation_order_time; ++i) tl2s.AT(i) = i;
  transfer_from_all (state,
                     tl  ,rl  ,ml,
                     & dh::fast_dboxes::fast_ref_prol_sendrecv,
                     tl2s,rl-1,ml,
                     time);
  timer.stop (0);
}



// Reflux a refinement level
void
ggf::
ref_reflux_all (comm_state & state,
                int const tl, int const rl, int const ml,
                int const dir, int const face)
{
  // Ignore the transport operator
  static Timer timer ("ref_reflux_all");
  timer.start ();
  // Require same times
  static_assert (abs(0.1) > 0, "Function CarpetLib::abs has wrong signature");
  assert (abs(t.get_time(ml,rl,tl) - t.get_time(ml,rl+1,tl)) <=
          1.0e-8 * (1.0 + abs(t.get_time(ml,rl,tl))));
  transfer_from_all (state,
                     tl,rl,ml,
                     dh::fast_dboxes::fast_ref_refl_sendrecv[dir][face],
                     tl,rl+1,ml);
  timer.stop (0);
}



// Transfer regions of all components
void
ggf::
transfer_from_all (comm_state & state,
                   int const tl1, int const rl1, int const ml1,
                   srpvect const dh::fast_dboxes::* sendrecvs,
                   vector<int> const & tl2s, int const rl2, int const ml2,
                   CCTK_REAL const & time,
                   mdata * const srcstorage_,
                   bool const flip_send_recv)
{
  assert (rl1>=0 and rl1<h.reflevels());
  assert (ml1>=0 and ml1<h.mglevels());
  assert (tl1>=0 and tl1<timelevels(ml1,rl1));
  
  srpvect const & psendrecvs = d.fast_boxes.AT(ml1).AT(rl1).*sendrecvs;
  
  // Return early if this communication does not concern us
  if (psendrecvs.empty()) return;
  
  static Timer total ("transfer_from_all");
  total.start ();
  
  mdata & srcstorage = srcstorage_ ? * srcstorage_ : storage;
  
  assert (           ml2<(int)srcstorage.size());
  assert (rl2>=0 and rl2<(int)srcstorage.AT(ml2).size());
  for (size_t i = 0; i < tl2s.size(); ++ i) {
    int const tl2 = tl2s.AT(i);
    assert (tl2>=0);
    int const lc = 0;
    if (lc < int(srcstorage.AT(ml2).AT(rl2).size())) {
      assert (tl2<(int)srcstorage.AT(ml2).AT(rl2).AT(lc).size());
    }
  }
  
  // Set up source times
  vector<CCTK_REAL> times(tl2s.size());
  for (size_t i=0; i<tl2s.size(); ++i) {
    assert (tl2s.AT(i)>=0 and tl2s.AT(i)<timelevels(ml2,rl2));
    for (size_t j=0; j<i; ++j) {
      assert (tl2s.AT(i) != tl2s.AT(j));
    }
    times.AT(i) = t.get_time(ml2,rl2,tl2s.AT(i));
  }
  
  // Interpolation orders
  assert (transport_operator != op_none);
  int const pos =
    transport_operator == op_copy or transport_operator == op_accumulate
    ? 0 : d.prolongation_orders_space.AT(rl2);
  int const pot =
    transport_operator == op_copy or transport_operator == op_accumulate
    ? 0 : prolongation_order_time;
  
  vector<const gdata*> gsrcs(tl2s.size());
  
  // Walk all regions
  for (srpvect::const_iterator ipsendrecv = psendrecvs.begin();
       ipsendrecv!=psendrecvs.end(); ++ ipsendrecv)
  {
    typedef sendrecv_pseudoregion_t srp;
    typedef pseudoregion_t sendrecv_pseudoregion_t::* srp_pr;
    srp_pr const send_field = flip_send_recv ? &srp::recv : &srp::send;
    srp_pr const recv_field = flip_send_recv ? &srp::send : &srp::recv;
    pseudoregion_t const & psend = (* ipsendrecv).*send_field;
    pseudoregion_t const & precv = (* ipsendrecv).*recv_field;
    ibbox const & send = psend.extent;
    ibbox const & recv = precv.extent;
    assert (all (send.stride() == h.baseextent(ml2,rl2).stride()));
    assert (all (recv.stride() == h.baseextent(ml1,rl1).stride()));
    int const c2 = psend.component;
    int const c1 = precv.component;
    int const lc2 = h.get_local_component(rl2,c2);
    int const lc1 = h.get_local_component(rl1,c1);
    int const p2 = h.processor(rl2,c2);
    int const p1 = h.processor(rl1,c1);
    // Ensure the communication schedule is consistent
    assert (p1==dist::rank() or p2==dist::rank());
    assert (lc1>=0 or lc2>=0);
    
    // Source and destination data
    gdata * const dst =
      lc1>=0 ? storage.AT(ml1).AT(rl1).AT(lc1).AT(tl1) : NULL;
    cdata const & srcs = srcstorage.AT(ml2).AT(rl2);
    for (int i=0; i<(int)gsrcs.size(); ++i) {
      gsrcs.AT(i) = lc2>=0 ? srcs.AT(lc2).AT(tl2s.AT(i)) : NULL;
    }
    
    dst->transfer_from
      (state, gsrcs, times, recv, send, p1, p2 , time, pos, pot);
  }
  
  total.stop (0);
}



// Memory usage
size_t
ggf::
memory ()
  const
{
  return
    memoryof (varindex) +
    memoryof (transport_operator) +
    memoryof (prolongation_order_time) +
    memoryof (timelevels_) +
    memoryof (storage) +
    memoryof (vectorlength) +
    memoryof (vectorindex) +
    memoryof (vectorleader) +
    memoryof (oldstorage);
}

size_t
ggf::
allmemory ()
{
  size_t mem = memoryof(allggf);
  for (list<ggf*>::const_iterator
         ggfi = allggf.begin(); ggfi != allggf.end(); ++ ggfi)
  {
    mem += memoryof(**ggfi);
  }
  return mem;
}
