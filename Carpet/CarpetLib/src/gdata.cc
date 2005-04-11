#include <cassert>
#include <cstdlib>
#include <iostream>

#include <mpi.h>

#include "cctk.h"
#include "cctk_Parameters.h"

#include "util_ErrorCodes.h"
#include "util_Table.h"

#include "bbox.hh"
#include "commstate.hh"
#include "defs.hh"
#include "dist.hh"
#include "vect.hh"

#include "gdata.hh"

using namespace std;



// Hand out the next MPI tag
static int nexttag ()
{
  DECLARE_CCTK_PARAMETERS;
  
  int const min_tag = 100;
  static int last = 0;
  ++last;
  if (last >= max_mpi_tags) last = 0;
  return min_tag + last;
}



// Constructors
gdata::gdata (const int varindex_, const operator_type transport_operator_,
              const int tag_)
  : varindex(varindex_), transport_operator(transport_operator_),
    _has_storage(false),
    comm_active(false),
    tag(tag_ >= 0 ? tag_ : nexttag())
{
  DECLARE_CCTK_PARAMETERS;
  if (barriers) {
    MPI_Barrier (dist::comm);
  }
}

// Destructors
gdata::~gdata ()
{
  DECLARE_CCTK_PARAMETERS;
  if (barriers) {
    MPI_Barrier (dist::comm);
  }
}



// Processor management
void gdata::change_processor (comm_state& state,
                              const int newproc,
                              void* const mem)
{
  DECLARE_CCTK_PARAMETERS;
  
  switch (state.thestate) {
  case state_recv:
    if (combine_recv_send) {
      change_processor_recv (state, newproc, mem);
      change_processor_send (state, newproc, mem);
    } else {
      change_processor_recv (state, newproc, mem);
    }
    break;
  case state_send:
    if (combine_recv_send) {
      // do nothing
    } else {
      change_processor_send (state, newproc, mem);
    }
    break;
  case state_wait:
    change_processor_wait (state, newproc, mem);
    break;
  default:
    assert(0);
  }
}



// Data manipulators
void gdata::copy_from (comm_state& state,
                       const gdata* src, const ibbox& box)
{
  DECLARE_CCTK_PARAMETERS;
  
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
  
  switch (state.thestate) {
  case state_recv:
    if (combine_recv_send) {
      copy_from_recv (state, src, box);
      copy_from_send (state, src, box);
    } else {
      copy_from_recv (state, src, box);
    }
    break;
  case state_send:
    if (combine_recv_send) {
      // do nothing
    } else {
      copy_from_send (state, src, box);
    }
    break;
  case state_wait:
    copy_from_wait (state, src, box);
    break;
  case state_get_buffer_sizes:
    // don't count processor-local copies
    if (proc() != src->proc()) {
      // if this is a destination processor: advance its recv buffer size
      if (proc() == dist::rank()) {
        state.collbufs.at(src->proc()).recvbufsize += box.size();
      }
      // if this is a source processor: increment its send buffer size
      if (src->proc() == dist::rank()) {
        state.collbufs.at(proc()).sendbufsize += box.size();
      }
    }
    break;
  case state_fill_send_buffers:
    // if this is a source processor: copy its data into the send buffer
    // (the processor-local case is also handled here)
    if (src->proc() == dist::rank()) {
      copy_into_sendbuffer (state, src, box);
    }
    break;
  case state_empty_recv_buffers:
    // if this is a destination processor and data has already been received
    // from the source processor: copy it from the recv buffer
    if (proc() == dist::rank() && state.recvbuffers_ready.at(src->proc())) {
      copy_from_recvbuffer (state, src, box);
    }
    break;
  default:
    assert(0);
  }
}



void gdata::copy_from_nocomm (const gdata* src, const ibbox& box)
{
  assert (has_storage() && src->has_storage());
  assert (proc() == src->proc());
  
  // copy on same processor
  if (dist::rank() == proc()) {
    copy_from_innerloop (src, box);
  }
}



void gdata::copy_from_recv (comm_state& state,
                            const gdata* src, const ibbox& box)
{
  DECLARE_CCTK_PARAMETERS;
  
  wtime_copyfrom_recv.start();
  
  if (proc() == src->proc()) {
    // copy on same processor
    
  } else {
    // copy to different processor
    
    if (! use_lightweight_buffers) {
      
      wtime_copyfrom_recv_maketyped.start();
      gdata* const tmp = make_typed(varindex, transport_operator);
      wtime_copyfrom_recv_maketyped.stop();
      state.tmps1.push (tmp);
      wtime_copyfrom_recv_allocate.start();
      tmp->allocate (box, src->proc());
      wtime_copyfrom_recv_allocate.stop();
      wtime_copyfrom_recv_changeproc_recv.start();
      tmp->change_processor_recv (state, proc());
      wtime_copyfrom_recv_changeproc_recv.stop();
      
    } else {
      
      if (dist::rank() == proc()) {
        // this processor receives data
        
        wtime_copyfrom_recvinner_allocate.start();
        comm_state::gcommbuf * b = make_typed_commbuf (box);
        wtime_copyfrom_recvinner_allocate.stop();
        
        wtime_copyfrom_recvinner_recv.start();
        MPI_Irecv (b->pointer(), b->size(), b->datatype(), src->proc(),
                   tag, dist::comm, &b->request);
        wtime_copyfrom_recvinner_recv.stop();
        if (use_waitall) {
          state.requests.push_back (b->request);
        }
        state.recvbufs.push (b);
        
      }
      
    }
    
  }
  
  wtime_copyfrom_recv.stop();
}

void gdata::copy_from_send (comm_state& state,
                            const gdata* src, const ibbox& box)
{
  DECLARE_CCTK_PARAMETERS;
  
  wtime_copyfrom_send.start();
  
  if (proc() == src->proc()) {
    // copy on same processor
    
    wtime_copyfrom_send_copyfrom_nocomm1.start();
    copy_from_nocomm (src, box);
    wtime_copyfrom_send_copyfrom_nocomm1.stop();
    
  } else {
    // copy to different processor
    
    if (! use_lightweight_buffers) {
      
      gdata* const tmp = state.tmps1.front();
      state.tmps1.pop();
      state.tmps2.push (tmp);
      assert (tmp);
      wtime_copyfrom_send_copyfrom_nocomm2.start();
      tmp->copy_from_nocomm (src, box);
      wtime_copyfrom_send_copyfrom_nocomm2.stop();
      wtime_copyfrom_send_changeproc_send.start();
      tmp->change_processor_send (state, proc());
      wtime_copyfrom_send_changeproc_send.stop();
      
    } else {
      
      if (dist::rank() == src->proc()) {
        // this processor sends data
        
        wtime_copyfrom_sendinner_allocate.start();
        comm_state::gcommbuf * b = src->make_typed_commbuf (box);
        wtime_copyfrom_sendinner_allocate.stop();
        
        wtime_copyfrom_sendinner_copy.start();
        assert (src->_has_storage);
        assert (src->_owns_storage);
        gdata * tmp = src->make_typed (varindex, transport_operator, tag);
        tmp->allocate (box, src->proc(), b->pointer());
        tmp->copy_from_innerloop (src, box);
        delete tmp;
        wtime_copyfrom_sendinner_copy.stop();
        
        wtime_copyfrom_sendinner_send.start();
        MPI_Isend (b->pointer(), b->size(), b->datatype(), proc(),
                   tag, dist::comm, &b->request);
        wtime_copyfrom_sendinner_send.stop();
        if (use_waitall) {
          state.requests.push_back (b->request);
        }
        state.sendbufs.push (b);
        
      }
    }
    
  }
  
  wtime_copyfrom_send.stop();
}

void gdata::copy_from_wait (comm_state& state,
                            const gdata* src, const ibbox& box)
{
  DECLARE_CCTK_PARAMETERS;
  
  wtime_copyfrom_wait.start();
  
  if (proc() == src->proc()) {
    // copy on same processor
    
  } else {
    // copy to different processor
    
    if (! use_lightweight_buffers) {
      
      gdata* const tmp = state.tmps2.front();
      state.tmps2.pop();
      assert (tmp);
      wtime_copyfrom_wait_changeproc_wait.start();
      tmp->change_processor_wait (state, proc());
      wtime_copyfrom_wait_changeproc_wait.stop();
      wtime_copyfrom_wait_copyfrom_nocomm.start();
      copy_from_nocomm (tmp, box);
      wtime_copyfrom_wait_copyfrom_nocomm.stop();
      wtime_copyfrom_wait_delete.start();
      delete tmp;
      wtime_copyfrom_wait_delete.stop();
      
    } else {
      
      if (dist::rank() == proc()) {
        // this processor receives data
        
        comm_state::gcommbuf * b = state.recvbufs.front();
        state.recvbufs.pop();
        
        wtime_copyfrom_recvwaitinner_wait.start();
        if (use_waitall) {
          if (! state.requests.empty()) {
            // wait for all requests at once
            MPI_Waitall (state.requests.size(), &state.requests.front(),
                         MPI_STATUSES_IGNORE);
            state.requests.clear();
          }
        }
        
        if (! use_waitall) {
          MPI_Wait (&b->request, MPI_STATUS_IGNORE);
        }
        wtime_copyfrom_recvwaitinner_wait.stop();
        
        wtime_copyfrom_recvwaitinner_copy.start();
        assert (_has_storage);
        assert (_owns_storage);
        gdata * tmp = make_typed (varindex, transport_operator, tag);
        tmp->allocate (box, proc(), b->pointer());
        copy_from_innerloop (tmp, box);
        delete tmp;
        wtime_copyfrom_recvwaitinner_copy.stop();
        
        wtime_copyfrom_recvwaitinner_delete.start();
        delete b;
        wtime_copyfrom_recvwaitinner_delete.stop();
        
      }
      
      if (dist::rank() == src->proc()) {
        // this processor sends data
        
        comm_state::gcommbuf * b = state.sendbufs.front();
        state.sendbufs.pop();
        
        wtime_copyfrom_sendwaitinner_wait.start();
        if (use_waitall) {
          if (! state.requests.empty()) {
            // wait for all requests at once
            MPI_Waitall (state.requests.size(), &state.requests.front(),
                         MPI_STATUSES_IGNORE);
            state.requests.clear();
          }
        }
        
        if (! use_waitall) {
          MPI_Wait (&b->request, MPI_STATUS_IGNORE);
        }
        wtime_copyfrom_sendwaitinner_wait.stop();
        
        wtime_copyfrom_sendwaitinner_delete.start();
        delete b;
        wtime_copyfrom_sendwaitinner_delete.stop();
        
      }
      
    }
    
  }
  
  wtime_copyfrom_wait.stop();
}


// Copy processor-local source data into communication send buffer
// of the corresponding destination processor
// The case when both source and destination are local is also handled here.
void gdata::copy_into_sendbuffer (comm_state& state,
                                  const gdata* src, const ibbox& box)
{
  if (proc() == src->proc()) {
    // copy on same processor
    copy_from_innerloop (src, box);
  } else {
    // copy to remote processor
    assert (src->_has_storage);
    assert (proc() < state.collbufs.size());
    int fillstate = (state.collbufs[proc()].sendbuf +
                     box.size()*state.vartypesize) -
                    state.collbufs[proc()].sendbufbase;
    assert (fillstate <= state.collbufs[proc()].sendbufsize*state.vartypesize);

    // copy this processor's data into the send buffer
    gdata* tmp = src->make_typed (varindex, transport_operator, tag);
    tmp->allocate (box, src->proc(), state.collbufs[proc()].sendbuf);
    tmp->copy_from_innerloop (src, box);
    delete tmp;

    // advance send buffer to point to the next ibbox slot
    state.collbufs[proc()].sendbuf += state.vartypesize * box.size();

    // post the send if the buffer is full
    if (fillstate == state.collbufs[proc()].sendbufsize*state.vartypesize) {
      MPI_Irsend (state.collbufs[proc()].sendbufbase,
                  state.collbufs[proc()].sendbufsize,
                  state.datatype, proc(), 0, dist::comm,
                  &state.srequests[proc()]);
    }
  }
}


// Copy processor-local destination data from communication recv buffer
// of the corresponding source processor
void gdata::copy_from_recvbuffer (comm_state& state,
                                  const gdata* src, const ibbox& box)
{
  assert (src->proc() < state.collbufs.size());
  assert (state.collbufs[src->proc()].recvbuf -
          state.collbufs[src->proc()].recvbufbase <=
          (state.collbufs[src->proc()].recvbufsize-box.size()) *
          state.vartypesize);

  // copy this processor's data from the recv buffer
  gdata* tmp = make_typed (varindex, transport_operator, tag);
  tmp->allocate (box, proc(), state.collbufs[src->proc()].recvbuf);
  copy_from_innerloop (tmp, box);
  delete tmp;

  // advance recv buffer to point to the next ibbox slot
  state.collbufs[src->proc()].recvbuf += state.vartypesize * box.size();
}



void gdata
::interpolate_from (comm_state& state,
                    const vector<const gdata*> srcs,
                    const vector<CCTK_REAL> times,
                    const ibbox& box, const CCTK_REAL time,
                    const int order_space,
                    const int order_time)
{
  DECLARE_CCTK_PARAMETERS;
  
  assert (transport_operator != op_error);
  if (transport_operator == op_none) return;

  assert (has_storage());
  assert (all(box.lower()>=extent().lower()));
  assert (all(box.upper()<=extent().upper()));
  assert (all(box.stride()==extent().stride()));
  assert (all((box.lower()-extent().lower())%box.stride() == 0));
  assert (srcs.size() == times.size() && srcs.size()>0);
  for (int t=0; t<(int)srcs.size(); ++t) {
    assert (srcs.at(t)->has_storage());
    assert (all(box.lower()>=srcs.at(t)->extent().lower()));
    assert (all(box.upper()<=srcs.at(t)->extent().upper()));
  }
  
  assert (! box.empty());
  if (box.empty()) return;
  
  switch (state.thestate) {
  case state_recv:
    if (combine_recv_send) {
      interpolate_from_recv (state, srcs, times, box, time, order_space, order_time);
      interpolate_from_send (state, srcs, times, box, time, order_space, order_time);
    } else {
      interpolate_from_recv (state, srcs, times, box, time, order_space, order_time);
    }
    break;
  case state_send:
    if (combine_recv_send) {
      // do nothing
    } else {
      interpolate_from_send (state, srcs, times, box, time, order_space, order_time);
    }
    break;
  case state_wait:
    interpolate_from_wait (state, srcs, times, box, time, order_space, order_time);
    break;
  case state_get_buffer_sizes:
    // don't count processor-local interpolations
    if (proc() != srcs.at(0)->proc()) {
      // if this is a destination processor: increment its recv buffer size
      if (proc() == dist::rank()) {
        state.collbufs.at(srcs.at(0)->proc()).recvbufsize += box.size();
      }
      // if this is a source processor: increment its send buffer size
      if (srcs.at(0)->proc() == dist::rank()) {
        state.collbufs.at(proc()).sendbufsize += box.size();
      }
    }
    break;
  case state_fill_send_buffers:
    // if this is a source processor: interpolate its data into the send buffer
    // (the processor-local case is also handled here)
    if (srcs.at(0)->proc() == dist::rank()) {
      interpolate_into_sendbuffer (state, srcs, times, box, time,
                                   order_space, order_time);
    }
    break;
  case state_empty_recv_buffers:
    // if this is a destination processor and data has already been received
    // from the source processor: copy it from the recv buffer
    // (the processor-local case is not handled here)
    if (proc() == dist::rank() && state.recvbuffers_ready.at(srcs.at(0)->proc())) {
      copy_from_recvbuffer (state, srcs.at(0), box);
    }
    break;
  default:
    assert(0);
  }
}



void gdata
::interpolate_from_nocomm (const vector<const gdata*> srcs,
                           const vector<CCTK_REAL> times,
                           const ibbox& box, const CCTK_REAL time,
                           const int order_space,
                           const int order_time)
{
  assert (proc() == srcs.at(0)->proc());
  
  assert (transport_operator != op_error);
  assert (transport_operator != op_none);
  
  // interpolate on same processor
  if (dist::rank() == proc()) {
    interpolate_from_innerloop
      (srcs, times, box, time, order_space, order_time);
  }
}



void gdata
::interpolate_from_recv (comm_state& state,
                         const vector<const gdata*> srcs,
                         const vector<CCTK_REAL> times,
                         const ibbox& box, const CCTK_REAL time,
                         const int order_space,
                         const int order_time)
{
  DECLARE_CCTK_PARAMETERS;
  
  if (proc() == srcs.at(0)->proc()) {
    // interpolate on same processor
    
  } else {
    // interpolate from other processor
    
    if (! use_lightweight_buffers) {
      
      gdata* const tmp = make_typed(varindex, transport_operator);
      state.tmps1.push (tmp);
      tmp->allocate (box, srcs.at(0)->proc());
      tmp->change_processor_recv (state, proc());
      
    } else {
      
      if (dist::rank() == proc()) {
        // this processor receives data
        
        comm_state::gcommbuf * b = make_typed_commbuf (box);
        
        MPI_Irecv (b->pointer(), b->size(), b->datatype(), srcs.at(0)->proc(),
                   tag, dist::comm, &b->request);
        if (use_waitall) {
          state.requests.push_back (b->request);
        }
        state.recvbufs.push (b);
        
      }
      
    }
    
  }
}



void gdata
::interpolate_from_send (comm_state& state,
                         const vector<const gdata*> srcs,
                         const vector<CCTK_REAL> times,
                         const ibbox& box, const CCTK_REAL time,
                         const int order_space,
                         const int order_time)
{
  DECLARE_CCTK_PARAMETERS;
  
  if (proc() == srcs.at(0)->proc()) {
    // interpolate on same processor
    
    interpolate_from_nocomm (srcs, times, box, time, order_space, order_time);
    
  } else {
    // interpolate from other processor
    
    if (! use_lightweight_buffers) {
      
      gdata* const tmp = state.tmps1.front();
      state.tmps1.pop();
      state.tmps2.push (tmp);
      assert (tmp);
      tmp->interpolate_from_nocomm
        (srcs, times, box, time, order_space, order_time);
      tmp->change_processor_send (state, proc());
      
    } else {
      
      if (dist::rank() == srcs.at(0)->proc()) {
        // this processor sends data
        
        comm_state::gcommbuf * b = srcs.at(0)->make_typed_commbuf (box);
        
        assert (srcs.at(0)->_has_storage);
        assert (srcs.at(0)->_owns_storage);
        gdata * tmp
          = srcs.at(0)->make_typed (varindex, transport_operator, tag);
        tmp->allocate (box, srcs.at(0)->proc(), b->pointer());
        tmp->interpolate_from_innerloop
          (srcs, times, box, time, order_space, order_time);
        delete tmp;
        
        MPI_Isend (b->pointer(), b->size(), b->datatype(), proc(),
                   tag, dist::comm, &b->request);
        if (use_waitall) {
          state.requests.push_back (b->request);
        }
        state.sendbufs.push (b);

      }
      
    }
    
  }
}



void gdata
::interpolate_from_wait (comm_state& state,
                         const vector<const gdata*> srcs,
                         const vector<CCTK_REAL> times,
                         const ibbox& box, const CCTK_REAL time,
                         const int order_space,
                         const int order_time)
{
  DECLARE_CCTK_PARAMETERS;
  
  if (proc() == srcs.at(0)->proc()) {
    // interpolate on same processor
    
  } else {
    // interpolate from other processor
    
    if (! use_lightweight_buffers) {
      
      gdata* const tmp = state.tmps2.front();
      state.tmps2.pop();
      assert (tmp);
      tmp->change_processor_wait (state, proc());
      copy_from_nocomm (tmp, box);
      delete tmp;
      
    } else {
      
      if (dist::rank() == proc()) {
        // this processor receives data
        
        comm_state::gcommbuf * b = state.recvbufs.front();
        state.recvbufs.pop();
        
        if (use_waitall) {
          if (! state.requests.empty()) {
            // wait for all requests at once
            MPI_Waitall (state.requests.size(), &state.requests.front(),
                         MPI_STATUSES_IGNORE);
            state.requests.clear();
          }
        }
        
        if (! use_waitall) {
          MPI_Wait (&b->request, MPI_STATUS_IGNORE);
        }
        
        assert (_has_storage);
        assert (_owns_storage);
        gdata * tmp = make_typed (varindex, transport_operator, tag);
        tmp->allocate (box, proc(), b->pointer());
        copy_from_innerloop (tmp, box);
        delete tmp;
        
        delete b;
        
      }
      
      if (dist::rank() == srcs.at(0)->proc()) {
        // this processor sends data
        
        comm_state::gcommbuf * b = state.sendbufs.front();
        state.sendbufs.pop();
        
        if (use_waitall) {
          if (! state.requests.empty()) {
            // wait for all requests at once
            MPI_Waitall (state.requests.size(), &state.requests.front(),
                         MPI_STATUSES_IGNORE);
            state.requests.clear();
          }
        }
        
        if (! use_waitall) {
          MPI_Wait (&b->request, MPI_STATUS_IGNORE);
        }
        
        delete b;
        
      }
      
    }
    
  }
}


// Interpolate processor-local source data into communication send buffer
// of the corresponding destination processor
// The case when both source and destination are local is also handled here.
void gdata
::interpolate_into_sendbuffer (comm_state& state,
                               const vector<const gdata*> srcs,
                               const vector<CCTK_REAL> times,
                               const ibbox& box,
                               const CCTK_REAL time,
                               const int order_space,
                               const int order_time)
{
  if (proc() == srcs.at(0)->proc()) {
    // interpolate on same processor
    interpolate_from_innerloop (srcs, times, box, time,
                                order_space, order_time);
  } else {
    // interpolate to remote processor
    assert (srcs.at(0)->_has_storage);
    assert (proc() < state.collbufs.size());
    int fillstate = (state.collbufs[proc()].sendbuf +
                     box.size()*state.vartypesize) -
                    state.collbufs[proc()].sendbufbase;
    assert (fillstate <= state.collbufs[proc()].sendbufsize*state.vartypesize);

    // copy this processor's data into the send buffer
    gdata* tmp = srcs.at(0)->make_typed (varindex, transport_operator, tag);
    tmp->allocate (box, srcs.at(0)->proc(), state.collbufs[proc()].sendbuf);
    tmp->interpolate_from_innerloop (srcs, times, box, time,
                                     order_space, order_time);
    delete tmp;

    // advance send buffer to point to the next ibbox slot
    state.collbufs[proc()].sendbuf += state.vartypesize * box.size();

    // post the send if the buffer is full
    if (fillstate == state.collbufs[proc()].sendbufsize*state.vartypesize) {
      MPI_Irsend (state.collbufs[proc()].sendbufbase,
                  state.collbufs[proc()].sendbufsize,
                  state.datatype, proc(), 0, dist::comm, 
                  &state.srequests[proc()]);
    }
  }
}
