// $Header: /home/eschnett/C/carpet/Carpet/Carpet/CarpetLib/src/gdata.cc,v 1.27 2004/03/23 12:14:29 schnetter Exp $

#include <assert.h>
#include <stdlib.h>

#include <iostream>

#include "cctk.h"

#include "util_ErrorCodes.h"
#include "util_Table.h"

#include "bbox.hh"
#include "defs.hh"
#include "dist.hh"
#include "vect.hh"

#include "gdata.hh"

using namespace std;



// Communication state control
template<int D>
comm_state<D>::comm_state ()
  : thestate(state_recv),
    current(0)
{
}

template<int D>
void comm_state<D>::step ()
{
  assert (thestate!=state_done);
  assert (current==tmps.size());
  thestate=astate(size_t(thestate)+1);
  current=0;
}

template<int D>
bool comm_state<D>::done ()
{
  return thestate==state_done;
}

template<int D>
comm_state<D>::~comm_state ()
{
  assert (thestate==state_recv || thestate==state_done);
}



// Hand out the next MPI tag
static int nexttag ()
{
  static int last = 100;
  ++last;
  if (last > 30000) last = 100;
  return last;
}



// Constructors
template<int D>
gdata<D>::gdata (const int varindex_, const operator_type transport_operator_)
  : varindex(varindex_), transport_operator(transport_operator_),
    wtime_isend(0.0), wtime_isendwait(0.0),
    wtime_irecv(0.0), wtime_irecvwait(0.0),
    _has_storage(false),
    comm_active(false),
    tag(nexttag())
{ }

// Destructors
template<int D>
gdata<D>::~gdata () { }



// Data manipulators
template<int D>
void gdata<D>::copy_from (comm_state<D>& state,
                          const gdata* src, const ibbox& box)
{
  switch (state.thestate) {
  case state_recv:
    copy_from_recv (state, src, box);
    break;
  case state_send:
    copy_from_send (state, src, box);
    break;
  case state_wait:
    copy_from_wait (state, src, box);
    break;
  default:
    assert(0);
  }
}



template<int D>
void gdata<D>::copy_from_nocomm (const gdata* src, const ibbox& box)
{
  assert (has_storage() && src->has_storage());
  assert (all(box.lower()>=extent().lower()
	      && box.lower()>=src->extent().lower()));
  assert (all(box.upper()<=extent().upper()
	      && box.upper()<=src->extent().upper()));
  assert (all(box.stride()==extent().stride()
	      && box.stride()==src->extent().stride()));
  assert (all((box.lower()-extent().lower())%box.stride() == 0
	      && (box.lower()-src->extent().lower())%box.stride() == 0));
  
  if (box.empty()) return;
  
  assert (proc() == src->proc());
  
  // copy on same processor
  int rank;
  MPI_Comm_rank (dist::comm, &rank);
  if (rank == proc()) {
    copy_from_innerloop (src, box);
  }
}



template<int D>
void gdata<D>::copy_from_recv (comm_state<D>& state,
                               const gdata* src, const ibbox& box)
{
  assert (has_storage() && src->has_storage());
  assert (all(box.lower()>=extent().lower()
	      && box.lower()>=src->extent().lower()));
  assert (all(box.upper()<=extent().upper()
	      && box.upper()<=src->extent().upper()));
  assert (all(box.stride()==extent().stride()
	      && box.stride()==src->extent().stride()));
  assert (all((box.lower()-extent().lower())%box.stride() == 0
	      && (box.lower()-src->extent().lower())%box.stride() == 0));
  
  if (box.empty()) return;
  
  if (proc() == src->proc()) {
    // copy on same processor
    
  } else {
    
    // copy to different processor
    gdata<D>* const tmp = make_typed(varindex, transport_operator);
    // TODO: is this efficient?
    state.tmps.push_back (tmp);
    ++state.current;
    tmp->allocate (box, src->proc());
    tmp->change_processor_recv (proc());
    
  }
}



template<int D>
void gdata<D>::copy_from_send (comm_state<D>& state,
                               const gdata* src, const ibbox& box)
{
  assert (has_storage() && src->has_storage());
  assert (all(box.lower()>=extent().lower()
	      && box.lower()>=src->extent().lower()));
  assert (all(box.upper()<=extent().upper()
	      && box.upper()<=src->extent().upper()));
  assert (all(box.stride()==extent().stride()
	      && box.stride()==src->extent().stride()));
  assert (all((box.lower()-extent().lower())%box.stride() == 0
	      && (box.lower()-src->extent().lower())%box.stride() == 0));
  
  if (box.empty()) return;
  
  if (proc() == src->proc()) {
    // copy on same processor
    
    copy_from_nocomm (src, box);
    
  } else {
    
    // copy to different processor
    gdata<D>* const tmp = state.tmps.at(state.current++);
    assert (tmp);
    tmp->copy_from_nocomm (src, box);
    tmp->change_processor_send (proc());
    
  }
}



template<int D>
void gdata<D>::copy_from_wait (comm_state<D>& state,
                               const gdata* src, const ibbox& box)
{
  assert (has_storage() && src->has_storage());
  assert (all(box.lower()>=extent().lower()
	      && box.lower()>=src->extent().lower()));
  assert (all(box.upper()<=extent().upper()
	      && box.upper()<=src->extent().upper()));
  assert (all(box.stride()==extent().stride()
	      && box.stride()==src->extent().stride()));
  assert (all((box.lower()-extent().lower())%box.stride() == 0
	      && (box.lower()-src->extent().lower())%box.stride() == 0));
  
  if (box.empty()) return;
  
  if (proc() == src->proc()) {
    // copy on same processor
    
  } else {
    
    // copy to different processor
    gdata<D>* const tmp = state.tmps.at(state.current++);
    assert (tmp);
    tmp->change_processor_wait (proc());
    copy_from_nocomm (tmp, box);
    delete tmp;
    
  }
}



template<int D>
void gdata<D>
::interpolate_from (comm_state<D>& state,
                    const vector<const gdata*> srcs,
                    const vector<CCTK_REAL> times,
                    const ibbox& box, const CCTK_REAL time,
                    const int order_space,
                    const int order_time)
{
  assert (transport_operator != op_error);
  if (transport_operator == op_none) return;
  switch (state.thestate) {
  case state_recv:
    interpolate_from_recv (state, srcs, times, box, time, order_space, order_time);
    break;
  case state_send:
    interpolate_from_send (state, srcs, times, box, time, order_space, order_time);
    break;
  case state_wait:
    interpolate_from_wait (state, srcs, times, box, time, order_space, order_time);
    break;
  default:
    assert(0);
  }
}



template<int D>
void gdata<D>
::interpolate_from_nocomm (const vector<const gdata*> srcs,
                           const vector<CCTK_REAL> times,
                           const ibbox& box, const CCTK_REAL time,
                           const int order_space,
                           const int order_time)
{
  assert (has_storage());
  assert (all(box.lower()>=extent().lower()));
  assert (all(box.upper()<=extent().upper()));
  assert (all(box.stride()==extent().stride()));
  assert (all((box.lower()-extent().lower())%box.stride() == 0));
  assert (srcs.size() == times.size() && srcs.size()>0);
  for (int t=0; t<(int)srcs.size(); ++t) {
    assert (srcs[t]->has_storage());
    assert (all(box.lower()>=srcs[t]->extent().lower()));
    assert (all(box.upper()<=srcs[t]->extent().upper()));
  }
  
  assert (! box.empty());
  if (box.empty()) return;
  
  assert (proc() == srcs[0]->proc());
  
  assert (transport_operator != op_error);
  assert (transport_operator != op_none);
  
  // interpolate on same processor
  int rank;
  MPI_Comm_rank (dist::comm, &rank);
  if (rank == proc()) {
    interpolate_from_innerloop
      (srcs, times, box, time, order_space, order_time);
  }
}



template<int D>
void gdata<D>
::interpolate_from_recv (comm_state<D>& state,
                         const vector<const gdata*> srcs,
                         const vector<CCTK_REAL> times,
                         const ibbox& box, const CCTK_REAL time,
                         const int order_space,
                         const int order_time)
{
  assert (has_storage());
  assert (all(box.lower()>=extent().lower()));
  assert (all(box.upper()<=extent().upper()));
  assert (all(box.stride()==extent().stride()));
  assert (all((box.lower()-extent().lower())%box.stride() == 0));
  assert (srcs.size() == times.size() && srcs.size()>0);
  for (int t=0; t<(int)srcs.size(); ++t) {
    assert (srcs[t]->has_storage());
    assert (all(box.lower()>=srcs[t]->extent().lower()));
    assert (all(box.upper()<=srcs[t]->extent().upper()));
  }
  
  assert (! box.empty());
  if (box.empty()) return;
  
  if (proc() == srcs[0]->proc()) {
    // interpolate on same processor
    
  } else {
    // interpolate from other processor
    
    gdata<D>* const tmp = make_typed(varindex, transport_operator);
    // TODO: is this efficient?
    state.tmps.push_back (tmp);
    ++state.current;
    tmp->allocate (box, srcs[0]->proc());
    tmp->change_processor_recv (proc());
    
  }
}



template<int D>
void gdata<D>
::interpolate_from_send (comm_state<D>& state,
                         const vector<const gdata*> srcs,
                         const vector<CCTK_REAL> times,
                         const ibbox& box, const CCTK_REAL time,
                         const int order_space,
                         const int order_time)
{
  assert (has_storage());
  assert (all(box.lower()>=extent().lower()));
  assert (all(box.upper()<=extent().upper()));
  assert (all(box.stride()==extent().stride()));
  assert (all((box.lower()-extent().lower())%box.stride() == 0));
  assert (srcs.size() == times.size() && srcs.size()>0);
  for (int t=0; t<(int)srcs.size(); ++t) {
    assert (srcs[t]->has_storage());
    assert (all(box.lower()>=srcs[t]->extent().lower()));
    assert (all(box.upper()<=srcs[t]->extent().upper()));
  }
  
  assert (! box.empty());
  if (box.empty()) return;
  
  if (proc() == srcs[0]->proc()) {
    // interpolate on same processor
    
    interpolate_from_nocomm (srcs, times, box, time, order_space, order_time);
    
  } else {
    // interpolate from other processor
    
    gdata<D>* const tmp = state.tmps.at(state.current++);
    assert (tmp);
    tmp->interpolate_from_nocomm (srcs, times, box, time, order_space, order_time);
    tmp->change_processor_send (proc());
    
  }
}



template<int D>
void gdata<D>
::interpolate_from_wait (comm_state<D>& state,
                         const vector<const gdata*> srcs,
                         const vector<CCTK_REAL> times,
                         const ibbox& box, const CCTK_REAL time,
                         const int order_space,
                         const int order_time)
{
  assert (has_storage());
  assert (all(box.lower()>=extent().lower()));
  assert (all(box.upper()<=extent().upper()));
  assert (all(box.stride()==extent().stride()));
  assert (all((box.lower()-extent().lower())%box.stride() == 0));
  assert (srcs.size() == times.size() && srcs.size()>0);
  for (int t=0; t<(int)srcs.size(); ++t) {
    assert (srcs[t]->has_storage());
    assert (all(box.lower()>=srcs[t]->extent().lower()));
    assert (all(box.upper()<=srcs[t]->extent().upper()));
  }
  
  assert (! box.empty());
  if (box.empty()) return;
  
  if (proc() == srcs[0]->proc()) {
    // interpolate on same processor
    
  } else {
    // interpolate from other processor
    
    gdata<D>* const tmp = state.tmps.at(state.current++);
    assert (tmp);
    tmp->change_processor_wait (proc());
    copy_from_nocomm (tmp, box);
    delete tmp;
    
  }
}



template class comm_state<3>;
template class gdata<3>;
