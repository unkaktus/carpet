#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <mpi.h>

#include "cctk.h"
#include "cctk_Parameters.h"

#include "bbox.hh"
#include "defs.hh"
#include "dist.hh"
#include "timestat.hh"
#include "vect.hh"

#include "data.hh"
#include "operator_prototypes.hh"

using namespace std;

using namespace CarpetLib;



// Fortran wrappers

template <typename T>
void
prolongate_3d_eno (T const * restrict const src,
                   ivect3 const & srcext,
                   T * restrict const dst,
                   ivect3 const & dstext,
                   ibbox3 const & srcbbox,
                   ibbox3 const & dstbbox,
                   ibbox3 const & regbbox)
{
  CCTK_WARN (0, "Data type not supported");
}

#ifndef OMIT_F90
extern "C"
void
CCTK_FCALL CCTK_FNAME(prolongate_3d_real8_eno)
  (const CCTK_REAL8* src,
   const int& srciext, const int& srcjext, const int& srckext,
   CCTK_REAL8* dst,
   const int& dstiext, const int& dstjext, const int& dstkext,
   const int srcbbox[3][3],
   const int dstbbox[3][3],
   const int regbbox[3][3]);

template <>
void
prolongate_3d_eno (CCTK_REAL8 const * restrict const src,
                   ivect3 const & srcext,
                   CCTK_REAL8 * restrict const dst,
                   ivect3 const & dstext,
                   ibbox3 const & srcbbox,
                   ibbox3 const & dstbbox,
                   ibbox3 const & regbbox)
{
  CCTK_FNAME(prolongate_3d_real8_eno)
    (src,
     srcext[0], srcext[1], srcext[2],
     dst,
     dstext[0], dstext[1], dstext[2],
     reinterpret_cast <int const (*) [3]> (& srcbbox),
     reinterpret_cast <int const (*) [3]> (& dstbbox),
     reinterpret_cast <int const (*) [3]> (& regbbox));
}
#endif



template <typename T>
void
prolongate_3d_weno (T const * restrict const src,
                    ivect3 const & srcext,
                    T * restrict const dst,
                    ivect3 const & dstext,
                    ibbox3 const & srcbbox,
                    ibbox3 const & dstbbox,
                    ibbox3 const & regbbox)
{
  CCTK_WARN (0, "Data type not supported");
}

#ifndef OMIT_F90
extern "C"
void
CCTK_FCALL CCTK_FNAME(prolongate_3d_real8_weno)
  (const CCTK_REAL8* src,
   const int& srciext, const int& srcjext, const int& srckext,
   CCTK_REAL8* dst,
   const int& dstiext, const int& dstjext, const int& dstkext,
   const int srcbbox[3][3],
   const int dstbbox[3][3],
   const int regbbox[3][3]);

template <>
void
prolongate_3d_weno (CCTK_REAL8 const * restrict const src,
                    ivect3 const & srcext,
                    CCTK_REAL8 * restrict const dst,
                    ivect3 const & dstext,
                    ibbox3 const & srcbbox,
                    ibbox3 const & dstbbox,
                    ibbox3 const & regbbox)
{
  CCTK_FNAME(prolongate_3d_real8_weno)
    (src,
     srcext[0], srcext[1], srcext[2],
     dst,
     dstext[0], dstext[1], dstext[2],
     reinterpret_cast <int const (*) [3]> (& srcbbox),
     reinterpret_cast <int const (*) [3]> (& dstbbox),
     reinterpret_cast <int const (*) [3]> (& regbbox));
}
#endif



static const CCTK_REAL eps = 1.0e-10;

// Constructors
template<typename T>
data<T>::data (const int varindex_,
               const centering cent_, const operator_type transport_operator_,
               const int vectorlength_, const int vectorindex_,
               data* const vectorleader_,
               const int tag_)
  : gdata(varindex_, cent_, transport_operator_, tag_),
    _memory(NULL),
    vectorlength(vectorlength_), vectorindex(vectorindex_),
    vectorleader(vectorleader_)
{
  assert (vectorlength>=1);
  assert (vectorindex>=0 && vectorindex<vectorlength);
  assert ((vectorindex==0 && not vectorleader)
          || (vectorindex!=0 && vectorleader));
}

template<typename T>
data<T>::data (const int varindex_,
               const centering cent_, const operator_type transport_operator_,
               const int vectorlength_, const int vectorindex_,
               data* const vectorleader_,
               const ibbox& extent_, const int proc_)
  : gdata(varindex_, cent_, transport_operator_),
    _memory(NULL),
    vectorlength(vectorlength_), vectorindex(vectorindex_),
    vectorleader(vectorleader_)
{
  assert (vectorlength>=1);
  assert (vectorindex>=0 && vectorindex<vectorlength);
  assert ((vectorindex==0 && not vectorleader)
          || (vectorindex!=0 && vectorleader));
  allocate(extent_, proc_);
}

// Destructors
template<typename T>
data<T>::~data ()
{
  if (_memory) free();
}
  
// Pseudo constructors
template<typename T>
data<T>* data<T>::make_typed (const int varindex_,
                              const centering cent_,
                              const operator_type transport_operator_,
                              const int tag_)
  const
{
  return new data(varindex_, cent_, transport_operator_, 1, 0, NULL, tag_);
}



// Storage management
template<typename T>
void data<T>::allocate (const ibbox& extent_,
                        const int proc_,
                        void* const memptr)
{
  assert (not _has_storage);
  _has_storage = true;
  // prevent accidental wrap-around
  assert (all(extent_.lower() < numeric_limits<int>::max() / 2));
  assert (all(extent_.lower() > numeric_limits<int>::min() / 2));
  assert (all(extent_.upper() < numeric_limits<int>::max() / 2));
  assert (all(extent_.upper() > numeric_limits<int>::min() / 2));
  // data
  _extent = extent_;
  _shape = max(ivect(0), _extent.shape() / _extent.stride());
  _size = 1;
  for (int d=0; d<dim; ++d) {
    _stride[d] = _size;
    assert (_shape[d]==0 || _size <= INT_MAX / _shape[d]);
    _size *= _shape[d];
  }
  _proc = proc_;
  if (dist::rank() == _proc) {
    if (vectorindex == 0) {
      assert (not vectorleader);
      _memory = new mem<T> (vectorlength, _size, (T*)memptr);
    } else {
      assert (vectorleader);
      _memory = vectorleader->_memory;
      assert (_memory);
    }
    _memory->register_client (vectorindex);
    _storage = _memory->storage(vectorindex);
  } else {
    assert (not memptr);
  }
}

template<typename T>
void data<T>::free ()
{
  assert (_has_storage);
  assert (_memory);
  _memory->unregister_client (vectorindex);
  if (not _memory->has_clients()) delete _memory;
  _memory = NULL;
  _has_storage = false;
  _storage = NULL;
}



// Processor management
template<typename T>
void data<T>::change_processor_recv (comm_state& state,
                                     const int newproc,
                                     void* const memptr)
{
  DECLARE_CCTK_PARAMETERS;
  
  assert (not comm_active);
  comm_active = true;
  
  if (newproc == _proc) {
    assert (not memptr);
    return;
  }
  
  static Timer total ("change_processor_recv");
  total.start ();
  
  assert (vectorlength == 1);
  
  if (_has_storage) {
    if (dist::rank() == newproc) {
      // copy from other processor
      
      assert (not _memory);
      _memory = new mem<T> (1, _size, (T*)memptr);
      _memory->register_client (0);
      _storage = _memory->storage (0);
      
      static Timer timer ("irecv");
      timer.start ();
      T dummy;
      MPI_Irecv (_memory->storage(0),
                 _size, dist::datatype(dummy), proc(),
                 tag, dist::comm(), &request);
      timer.stop (_size * sizeof(T));
      if (use_waitall) {
        state.requests.push_back (request);
      }
      
    } else if (dist::rank() == _proc) {
      // copy to other processor
      
    } else {
      assert (not memptr);
      assert (not _memory);
    }
  }
  
  total.stop (0);
}



template<typename T>
void data<T>::change_processor_send (comm_state& state,
                                     const int newproc,
                                     void* const memptr)
{
  DECLARE_CCTK_PARAMETERS;
  
  assert (comm_active);
  
  if (newproc == _proc) {
    assert (not memptr);
    return;
  }
  
  static Timer total ("change_processor_send");
  total.start();
  
  assert (vectorlength == 1);
  
  if (_has_storage) {
    if (dist::rank() == newproc) {
      // copy from other processor
      
    } else if (dist::rank() == _proc) {
      // copy to other processor
      
      assert (not memptr);
      assert (_memory);
      
      static Timer timer ("isend");
      timer.start ();
      T dummy;
      MPI_Isend (_memory->storage(0),
                 _size, dist::datatype(dummy), newproc,
                 tag, dist::comm(), &request);
      timer.stop (_size * sizeof(T));
      if (use_waitall) {
        state.requests.push_back (request);
      }
      
    } else {
      assert (not memptr);
      assert (not _memory);
    }
  }
  
  total.stop (0);
}



template<typename T>
void data<T>::change_processor_wait (comm_state& state,
                                     const int newproc,
                                     void* const memptr)
{
  DECLARE_CCTK_PARAMETERS;
  
  assert (comm_active);
  comm_active = false;
  
  if (newproc == _proc) {
    assert (not memptr);
    return;
  }
  
  static Timer total ("change_processor_wait");
  total.start ();
  
  assert (vectorlength == 1);
  
  if (use_waitall) {
    if (not state.requests.empty()) {
      // wait for all requests at once
      static Timer timer ("irecvwait");
      timer.start ();
      MPI_Waitall
        (state.requests.size(), &state.requests.front(), MPI_STATUSES_IGNORE);
      timer.stop (0);
      state.requests.clear();
    }
  }
  
  if (_has_storage) {
    if (dist::rank() == newproc) {
      // copy from other processor
      
      if (not use_waitall) {
        static Timer timer ("irecvwait");
        timer.start ();
        MPI_Wait (&request, MPI_STATUS_IGNORE);
        timer.stop (0);
      }
      
    } else if (dist::rank() == _proc) {
      // copy to other processor
      
      assert (not memptr);
      assert (_memory);
      
      if (not use_waitall) {
        static Timer timer ("isendwait");
        timer.start ();
        MPI_Wait (&request, MPI_STATUS_IGNORE);
        timer.stop (0);
      }
      
      _memory->unregister_client (0);
      if (not _memory->has_clients()) delete _memory;
      _memory = NULL;;
      _storage = NULL;
      
    } else {
      assert (not memptr);
      assert (not _memory);
    }
  }
  
  _proc = newproc;
  
  total.stop (0);
}



// Data manipulators
template<typename T>
comm_state::gcommbuf *
data<T>::
make_typed_commbuf (const ibbox & box)
  const
{
  return new comm_state::commbuf<T> (box);
}



template <typename T>
void data <T>
::copy_from_innerloop (gdata const * const gsrc,
                       ibbox const & box)
{
  data const * const src = dynamic_cast <data const *> (gsrc);
  assert (has_storage() and src->has_storage());
  
  assert (proc() == src->proc());
  assert (dist::rank() == proc());
  
  copy_3d (static_cast <T const *> (src->storage()),
           src->shape(),
           static_cast <T *> (this->storage()),
           this->shape(),
           src->extent(),
           this->extent(),
           box);
}



template <typename T>
void data <T>
::interpolate_from_innerloop (vector <gdata const *> const & gsrcs,
                              vector <CCTK_REAL> const & times,
			      ibbox const & box,
                              CCTK_REAL const time,
                              int const order_space,
			      int const order_time)
{
  assert (has_storage());
  
  vector <data const *> srcs (gsrcs.size());
  for (size_t t=0; t<srcs.size(); ++t) {
    srcs.AT(t) = dynamic_cast <data const *> (gsrcs.AT(t));
  }
  assert (srcs.size() == times.size() and srcs.size() > 0);
  
  for (size_t t=0; t<srcs.size(); ++t) {
    assert (srcs.AT(t)->has_storage());
    assert (proc() == srcs.AT(t)->proc());
  }
  
  assert (dist::rank() == proc());
  
  interpolate_time (srcs, times, box, time, order_space, order_time);
}



template <typename T>
void data <T>
::interpolate_time (vector <data const *> const & srcs,
                    vector <CCTK_REAL> const & times,
                    ibbox const & box,
                    CCTK_REAL const time,
                    int const order_space,
                    int const order_time)
{
  // Ensure that the times are consistent
  assert (times.size() > 0);
  CCTK_REAL const min_time = * min_element (times.begin(), times.end());
  CCTK_REAL const max_time = * max_element (times.begin(), times.end());
  if (transport_operator != op_copy) {
    if (time < min_time - eps or time > max_time + eps) {
      ostringstream buf;
      buf << "Internal error: extrapolation in time."
          << "  time=" << time
          << "  times=" << times;
      CCTK_WARN (0, buf.str().c_str());
    }
  }
  
  // Use this timelevel, or interpolate in time if set to -1
  int timelevel = -1;
  
  // Try to avoid time interpolation if possible
  if (timelevel == -1) {
    if (times.size() == 1) {
      timelevel = 0;
    }
  }
  if (timelevel == -1) {
    if (transport_operator == op_copy) {
      timelevel = 0;
    }
  }
  if (timelevel == -1) {
    for (size_t tl=0; tl<times.size(); ++tl) {
      if (abs (times.AT(tl) - time) < eps) {
        timelevel = tl;
        break;
      }
    }
  }
  
  if (timelevel == -1) {
    // Time interpolation is necessary
    
    vector <data *> tmps (times.size());
    
    for (size_t tl=0; tl<times.size(); ++tl) {
      
      tmps.AT(tl) =
        new data (this->varindex, this->cent, this->transport_operator);
      tmps.AT(tl)->allocate (box, this->proc());
      
      tmps.AT(tl)->interpolate_p_r (srcs.AT(tl), box, order_space);
      
    }
    
    time_interpolate (tmps, box, times, time, order_time);
    
    for (size_t tl=0; tl<times.size(); ++tl) {
      delete tmps.AT(tl);
    }
    
  } else {
    // No time interpolation
    
    interpolate_p_r (srcs.AT(timelevel), box, order_space);
    
  } // if
}



template <typename T>
void data <T>
::interpolate_p_r (data const * const src,
                   ibbox const & box,
                   int const order_space)
{
  if (all (src->extent().stride() > this->extent().stride())) {
    // Prolongate
    interpolate_p_vc_cc (src, box, order_space);
  } else if (all (src->extent().stride() < this->extent().stride())) {
    // Restrict
    interpolate_restrict (src, box, order_space);
  } else {
    assert (0);
  }
}



template <typename T>
void data <T>
::interpolate_p_vc_cc (data const * const src,
                       ibbox const & box,
                       int const order_space)
{
  if (cent == vertex_centered) {
    // Vertex centred
    
    interpolate_prolongate (src, box, order_space);
    
  } else if (cent == cell_centered) {
    // Cell centred
    
    // Destination region
    assert (all (box.stride() % 2 == 0));
    ibbox const newdstbox (box.lower() - box.stride() / 2,
                           box.upper() + box.stride() / 2,
                           box.stride());
    
    // Source region
    ibbox const & srcbox = src->extent();
    
    assert (all (srcbox.stride() % 2 == 0));
    ibbox const tmpsrcbox (srcbox.lower() - srcbox.stride() / 2,
                           srcbox.upper() + srcbox.stride() / 2,
                           srcbox.stride());
    
    assert (all (srcbox.stride() % box.stride() == 0));
    ivect const reffact = srcbox.stride() / box.stride();
    
    ivect const regext = newdstbox.shape() / newdstbox.stride();
    assert (all ((newdstbox.lower() - srcbox.lower()) % box.stride() == 0));
    ivect const srcoff = (newdstbox.lower() - srcbox.lower()) / box.stride();
    
    bvect const needoffsetlo =
      srcoff % reffact != 0 or regext > 1;
    bvect const needoffsethi =
      (srcoff + regext - 1) % reffact != 0 or regext > 1;
    
    assert (order_space % 2 == 1);
    int const stencil_size = (order_space + 1) / 2;
    
    ivect const offsetlo = either (needoffsetlo, stencil_size, 0);
    ivect const offsethi = either (needoffsethi, stencil_size, 0);
    
    ibbox const newsrcbox =
      newdstbox .contracted_for (tmpsrcbox) .expand (offsetlo, offsethi);
    
    // Allocate temporary storage
    data * const newsrc =
      new data (src->varindex, vertex_centered, src->transport_operator);
    newsrc->allocate (newsrcbox, src->proc());
    
    data * const newdst =
      new data (this->varindex, vertex_centered, this->transport_operator);
    newdst->allocate (newdstbox, this->proc());
    
    // Convert source to primitive representation
    prolongate_3d_cc_rf2_std2prim
      (static_cast <T const *> (src->storage()),
       src->shape(),
       static_cast <T *> (newsrc->storage()),
       newsrc->shape(),
       src->extent(),
       newsrc->extent(),
       newsrc->extent());
    
    // Interpolate
    newdst->interpolate_prolongate (newsrc, newdstbox, order_space);
    
    // Convert destination to standard representation
    prolongate_3d_cc_rf2_prim2std
      (static_cast <T const *> (newdst->storage()),
       newdst->shape(),
       static_cast <T *> (this->storage()),
       this->shape(),
       newdst->extent(),
       this->extent(),
       box);
    
    delete newsrc;
    delete newdst;
    
  } else {
    assert (0);
  }
}

template <>
void data <CCTK_INT>
::interpolate_p_vc_cc (data const * const src,
                       ibbox const & box,
                       int const order_space)
{
  CCTK_WARN (0, "Data type not supported");
}



template <typename T>
void data <T>
::interpolate_prolongate (data const * const src,
                          ibbox const & box,
                          int const order_space)
{
  static Timer total ("prolongate");
  total.start ();
  
  switch (transport_operator) {
    
  case op_copy:
  case op_Lagrange: {
    static Timer timer ("prolongate_Lagrange");
    timer.start ();
    switch (order_space) {
    case 1:
      prolongate_3d_o1_rf2 (static_cast <T const *> (src->storage()),
                            src->shape(),
                            static_cast <T *> (this->storage()),
                            this->shape(),
                            src->extent(),
                            this->extent(),
                            box);
      break;
    case 3:
      prolongate_3d_o3_rf2 (static_cast <T const *> (src->storage()),
                            src->shape(),
                            static_cast <T *> (this->storage()),
                            this->shape(),
                            src->extent(),
                            this->extent(),
                            box);
      break;
    case 5:
      prolongate_3d_o5_rf2 (static_cast <T const *> (src->storage()),
                            src->shape(),
                            static_cast <T *> (this->storage()),
                            this->shape(),
                            src->extent(),
                            this->extent(),
                            box);
      break;
    default:
      assert (0);
    }
    timer.stop (0);
    break;
  }
    
  case op_ENO: {
    static Timer timer ("prolongate_ENO");
    timer.start ();
    switch (order_space) {
    case 1:
      CCTK_WARN (0, "There is no stencil for op=\"ENO\" with order_space=1");
      break;
    case 3:
      prolongate_3d_eno (static_cast <T const *> (src->storage()),
                         src->shape(),
                         static_cast <T *> (this->storage()),
                         this->shape(),
                         src->extent(),
                         this->extent(),
                         box);
      break;
    default:
      assert (0);
    }
    timer.stop (0);
    break;
  }
    
  case op_WENO: {
    static Timer timer ("prolongate_WENO");
    timer.start ();
    switch (order_space) {
    case 1:
      CCTK_WARN (0, "There is no stencil for op=\"WENO\" with order_space=1");
      break;
    case 3:
      CCTK_WARN (0, "There is no stencil for op=\"WENO\" with order_space=3");
      break;
    case 5:
      prolongate_3d_eno (static_cast <T const *> (src->storage()),
                         src->shape(),
                         static_cast <T *> (this->storage()),
                         this->shape(),
                         src->extent(),
                         this->extent(),
                         box);
      break;
    default:
      assert (0);
    }
    timer.stop (0);
    break;
  }
    
  default:
    assert (0);
  } // switch (transport_operator)
  
  total.stop (0);
}

template <>
void data <CCTK_INT>
::interpolate_prolongate (data const * const src,
                          ibbox const & box,
                          int const order_space)
{
  CCTK_WARN (0, "Data type not supported");
}



template <typename T>
void data <T>
::interpolate_restrict (data const * const src,
                        ibbox const & box,
                        int const order_space)
{
  static Timer total ("restrict");
  total.start ();
  
  switch (transport_operator) {
    
  case op_copy:
  case op_Lagrange:
  case op_ENO:
  case op_WENO:
    // enum centering { vertex_centered, cell_centered };
    switch (cent) {
    case vertex_centered:
      restrict_3d_rf2 (static_cast <T const *> (src->storage()),
                       src->shape(),
                       static_cast <T *> (this->storage()),
                       this->shape(),
                       src->extent(),
                       this->extent(),
                       box);
      break;
    case cell_centered:
      restrict_3d_cc_rf2 (static_cast <T const *> (src->storage()),
                          src->shape(),
                          static_cast <T *> (this->storage()),
                          this->shape(),
                          src->extent(),
                          this->extent(),
                          box);
      break;
    default:
      assert (0);
    }
    break;
    
  default:
    assert (0);
  }
  
  total.stop (0);
}

template <>
void data <CCTK_INT>
::interpolate_restrict (data const * const src,
                        ibbox const & box,
                        int const order_space)
{
  CCTK_WARN (0, "Data type not supported");
}



template <typename T>
void data <T>
::time_interpolate (vector <data *> const & srcs,
                    ibbox const & box,
                    vector <CCTK_REAL> const & times,
                    CCTK_REAL const time,
                    int const order_time)
{
  switch (order_time) {
    
  case 0:
    // We could handle this, but this points to an inefficiency
    assert (0);
    
  case 1:
    assert (times.size() >= 2);
    interpolate_3d_2tl (static_cast <T const *> (srcs.AT(0)->storage()),
                        times.AT(0),
                        static_cast <T const *> (srcs.AT(1)->storage()),
                        times.AT(1),
                        srcs.AT(0)->shape(),
                        static_cast <T *> (this->storage()),
                        time,
                        this->shape(),
                        srcs.AT(0)->extent(),
                        this->extent(),
                        box);
    break;
    
  case 2:
    assert (times.size() >= 3);
    interpolate_3d_3tl (static_cast <T const *> (srcs.AT(0)->storage()),
                        times.AT(0),
                        static_cast <T const *> (srcs.AT(1)->storage()),
                        times.AT(1),
                        static_cast <T const *> (srcs.AT(2)->storage()),
                        times.AT(2),
                        srcs.AT(0)->shape(),
                        static_cast <T *> (this->storage()),
                        time,
                        this->shape(),
                        srcs.AT(0)->extent(),
                        this->extent(),
                        box);
    break;
    
  default:
    assert (0);
  }
}

template <>
void data <CCTK_INT>
::time_interpolate (vector <data *> const & srcs,
                    ibbox const & box,
                    vector <CCTK_REAL> const & times,
                    CCTK_REAL const time,
                    int const order_time)
{
  CCTK_WARN (0, "Data type not supported");
}



// Output
template<typename T>
ostream& data<T>::output (ostream& os) const
{
  T Tdummy;
  os << "data<" << typestring(Tdummy) << ">:"
     << "extent=" << extent() << ","
     << "stride=" << stride() << ",size=" << size();
  return os;
}

template<typename T>
ostream & operator << (ostream & os, const data<T> & d)
{
  char const * space = "";
  for (int i = 0; i < d.vectorlength; i++) {
    os << space << d[i];
    space = " ";
  }
  return os;
}



#define INSTANTIATE(T)				\
template class data<T>;
template 
ostream & operator << <CCTK_REAL> (ostream & os, data<CCTK_REAL> const & d);
#include "instantiate"

#undef INSTANTIATE
