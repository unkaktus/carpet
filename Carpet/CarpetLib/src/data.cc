/***************************************************************************
                          data.cc  -  Data storage
                             -------------------
    begin                : Sun Jun 11 2000
    copyright            : (C) 2000 by Erik Schnetter
    email                : schnetter@astro.psu.edu

    $Header: /home/eschnett/C/carpet/Carpet/Carpet/CarpetLib/src/data.cc,v 1.20 2002/09/25 15:49:15 schnetter Exp $

 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <assert.h>

#include <iostream>
#include <string>

#include <mpi.h>

#include "cctk.h"

#include "bbox.hh"
#include "defs.hh"
#include "dist.hh"
#include "vect.hh"

#include "data.hh"

using namespace std;



// Constructors
template<class T, int D>
data<T,D>::data ()
  : _storage(0)
{ }

template<class T, int D>
data<T,D>::data (const ibbox& extent, const int proc)
  : _storage(0)
{
  allocate(extent, proc);
}

// Destructors
template<class T, int D>
data<T,D>::~data () {
  free();
}
  
// Pseudo constructors
template<class T, int D>
data<T,D>* data<T,D>::make_typed () const {
  return new data();
}



// Storage management
template<class T, int D>
void data<T,D>::allocate (const ibbox& extent, const int proc,
			  void* const mem) {
  assert (!_has_storage);
  _has_storage = true;
  // data
  _extent = extent;
  _shape = max(ivect(0), _extent.shape() / _extent.stride());
  _size = 1;
  for (int d=0; d<D; ++d) {
    _stride[d] = _size;
    _size *= _shape[d];
  }
  _proc = proc;
  int rank;
  MPI_Comm_rank (dist::comm, &rank);
  if (rank==_proc) {
    _owns_storage = !mem;
    if (_owns_storage) {
      _storage = new T[_size];
    } else {
      _storage = (T*)mem;
    }
  } else {
    assert (!mem);
  }
}

template<class T, int D>
void data<T,D>::free () {
  if (_storage && _owns_storage) delete [] _storage;
  _storage = 0;
  _has_storage = false;
}

template<class T, int D>
void data<T,D>::transfer_from (generic_data<D>* gsrc) {
  data* src = (data*)gsrc;
  assert (!_storage);
  *this = *src;
  *src = data();
}



// Processor management
template<class T, int D>
void data<T,D>::change_processor (const int newproc, void* const mem) {
  if (newproc == _proc) {
    assert (!mem);
    return;
  }
  
  if (_has_storage) {
    int rank;
    MPI_Comm_rank (dist::comm, &rank);
    if (rank == newproc) {
      // copy from other processor
      
      assert (!_storage);
      _owns_storage = !mem;
      if (_owns_storage) {
	_storage = new T[_size];
      } else {
	_storage = (T*)mem;
      }

      T dummy;
      MPI_Status status;
      MPI_Recv (_storage, _size, dist::datatype(dummy), _proc,
		dist::tag, dist::comm, &status);

    } else if (rank == _proc) {
      // copy to other processor
      
      assert (!mem);
      assert (_storage);
      T dummy;
      MPI_Send (_storage, _size, dist::datatype(dummy), newproc,
		dist::tag, dist::comm);

      if (_owns_storage) {
	delete [] _storage;
      }
      _storage = 0;
      
    } else {
      assert (!mem);
      assert (!_storage);
    }
  }

  _proc = newproc;
}



// Data manipulators
template<class T, int D>
void data<T,D>
::copy_from_innerloop (const generic_data<D>* gsrc, const ibbox& box)
{
  const data* src = (const data*)gsrc;
  assert (has_storage() && src->has_storage());
  assert (all(box.lower()>=extent().lower()
	      && box.lower()>=src->extent().lower()));
  assert (all(box.upper()<=extent().upper()
	      && box.upper()<=src->extent().upper()));
  assert (all(box.stride()==extent().stride()
	      && box.stride()==src->extent().stride()));
  assert (all((box.lower()-extent().lower())%box.stride() == 0
	      && (box.lower()-src->extent().lower())%box.stride() == 0));
  
  assert (proc() == src->proc());
  
  int rank;
  MPI_Comm_rank (dist::comm, &rank);
  assert (rank == proc());
  
  for (typename ibbox::iterator it=box.begin(); it!=box.end(); ++it) {
    const ivect index = *it;
    (*this)[index] = (*src)[index];
  }
  
}



template<class T, int D>
void data<T,D>
::interpolate_from_innerloop (const vector<const generic_data<D>*> gsrcs,
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
  vector<const data*> srcs(gsrcs.size());
  for (int t=0; t<(int)srcs.size(); ++t) srcs[t] = (const data*)gsrcs[t];
  assert (srcs.size() == times.size() && srcs.size()>0);
  for (int t=0; t<(int)srcs.size(); ++t) {
    assert (srcs[t]->has_storage());
    assert (all(box.lower()>=srcs[t]->extent().lower()));
    assert (all(box.upper()<=srcs[t]->extent().upper()));
    assert (proc() == srcs[t]->proc());
  }
  
  int rank;
  MPI_Comm_rank (dist::comm, &rank);
  assert (rank == proc());
  
  assert (order_space <= 1);
  
  assert ((int)srcs.size() > order_time);
  vector<T> src_fac(order_time+1);
  for (int t=0; t<(int)src_fac.size(); ++t) {
    src_fac[t] = 1;
    for (int tt=0; tt<(int)src_fac.size(); ++tt) {
      if (tt!=t) {
	src_fac[t] *= (T)(time - times[tt]) / (T)(times[t] - times[tt]);
      }
    }
  }
  
  for (typename ibbox::iterator posi=box.begin(); posi!=box.end(); ++posi) {
    const ivect& pos = *posi;
    
    // get box around current position
    const ibbox frombox
      = ibbox(pos,pos,extent().stride()).expanded_for(srcs[0]->extent());
    
    // interpolate from box to position
    T sum = 0;
    for (typename ibbox::iterator fromposi=frombox.begin();
	 fromposi!=frombox.end(); ++fromposi)
      {
	const ivect& frompos = *fromposi;
	
	// interpolation weight
	const ivect str = srcs[0]->extent().stride();
	const T f = prod(vect<T,D>(str - abs(pos - frompos)) / vect<T,D>(str));
	for (int t=0; t<(int)src_fac.size(); ++t) {
	  sum += f * src_fac[t] * (*srcs[t])[frompos];
	}
      }
    (*this)[pos] = sum;
    
  } // for pos
  
}



extern "C" {
  void CCTK_FCALL CCTK_FNAME(copy_3d_real8)
    (const CCTK_REAL8* src,
     const int& srciext, const int& srcjext, const int& srckext,
     CCTK_REAL8* dst,
     const int& dstiext, const int& dstjext, const int& dstkext,
     const int srcbbox[3][3],
     const int dstbbox[3][3],
     const int regbbox[3][3]);
}

template<>
void data<CCTK_REAL8,3>
::copy_from_innerloop (const generic_data<3>* gsrc, const ibbox& box)
{
  const data* src = (const data*)gsrc;
  assert (has_storage() && src->has_storage());
  assert (all(box.lower()>=extent().lower()
	      && box.lower()>=src->extent().lower()));
  assert (all(box.upper()<=extent().upper()
	      && box.upper()<=src->extent().upper()));
  assert (all(box.stride()==extent().stride()
	      && box.stride()==src->extent().stride()));
  assert (all((box.lower()-extent().lower())%box.stride() == 0
	      && (box.lower()-src->extent().lower())%box.stride() == 0));
  
  assert (proc() == src->proc());
  
  int rank;
  MPI_Comm_rank (dist::comm, &rank);
  assert (rank == proc());
  
  const ibbox& sext = src->extent();
  const ibbox& dext = extent();
  
  int srcshp[3], dstshp[3];
  int srcbbox[3][3], dstbbox[3][3], regbbox[3][3];
  
  for (int d=0; d<3; ++d) {
    srcshp[d] = (sext.shape() / sext.stride())[d];
    dstshp[d] = (dext.shape() / dext.stride())[d];
    
    srcbbox[0][d] = sext.lower()[d];
    srcbbox[1][d] = sext.upper()[d];
    srcbbox[2][d] = sext.stride()[d];
    
    dstbbox[0][d] = dext.lower()[d];
    dstbbox[1][d] = dext.upper()[d];
    dstbbox[2][d] = dext.stride()[d];
    
    regbbox[0][d] = box.lower()[d];
    regbbox[1][d] = box.upper()[d];
    regbbox[2][d] = box.stride()[d];
  }
  
  assert (all(dext.stride() == box.stride()));
  if (all(sext.stride() == dext.stride())) {
    CCTK_FNAME(copy_3d_real8) ((const CCTK_REAL8*)src->storage(),
			       srcshp[0], srcshp[1], srcshp[2],
			       (CCTK_REAL8*)storage(),
			       dstshp[0], dstshp[1], dstshp[2],
			       srcbbox,
			       dstbbox,
			       regbbox);
    
  } else {
    assert (0);
  }
}



extern "C" {
  
  void CCTK_FCALL CCTK_FNAME(restrict_3d_real8)
    (const CCTK_REAL8* src,
     const int& srciext, const int& srcjext, const int& srckext,
     CCTK_REAL8* dst,
     const int& dstiext, const int& dstjext, const int& dstkext,
     const int srcbbox[3][3],
     const int dstbbox[3][3],
     const int regbbox[3][3]);
  
  
  
  void CCTK_FCALL CCTK_FNAME(prolongate_3d_real8)
    (const CCTK_REAL8* src,
     const int& srciext, const int& srcjext, const int& srckext,
     CCTK_REAL8* dst,
     const int& dstiext, const int& dstjext, const int& dstkext,
     const int srcbbox[3][3],
     const int dstbbox[3][3],
     const int regbbox[3][3]);
  void CCTK_FCALL CCTK_FNAME(prolongate_3d_real8_o3)
    (const CCTK_REAL8* src,
     const int& srciext, const int& srcjext, const int& srckext,
     CCTK_REAL8* dst,
     const int& dstiext, const int& dstjext, const int& dstkext,
     const int srcbbox[3][3],
     const int dstbbox[3][3],
     const int regbbox[3][3]);
  
  void CCTK_FCALL CCTK_FNAME(prolongate_3d_real8_2tl)
    (const CCTK_REAL8* src1, const CCTK_REAL8& t1,
     const CCTK_REAL8* src2, const CCTK_REAL8& t2,
     const int& srciext, const int& srcjext, const int& srckext,
     CCTK_REAL8* dst, const CCTK_REAL8& t,
     const int& dstiext, const int& dstjext, const int& dstkext,
     const int srcbbox[3][3],
     const int dstbbox[3][3],
     const int regbbox[3][3]);
  void CCTK_FCALL CCTK_FNAME(prolongate_3d_real8_2tl_o3)
    (const CCTK_REAL8* src1, const CCTK_REAL8& t1,
     const CCTK_REAL8* src2, const CCTK_REAL8& t2,
     const int& srciext, const int& srcjext, const int& srckext,
     CCTK_REAL8* dst, const CCTK_REAL8& t,
     const int& dstiext, const int& dstjext, const int& dstkext,
     const int srcbbox[3][3],
     const int dstbbox[3][3],
     const int regbbox[3][3]);
  
  void CCTK_FCALL CCTK_FNAME(prolongate_3d_real8_3tl)
    (const CCTK_REAL8* src1, const CCTK_REAL8& t1,
     const CCTK_REAL8* src2, const CCTK_REAL8& t2,
     const CCTK_REAL8* src3, const CCTK_REAL8& t3,
     const int& srciext, const int& srcjext, const int& srckext,
     CCTK_REAL8* dst, const CCTK_REAL8& t,
     const int& dstiext, const int& dstjext, const int& dstkext,
     const int srcbbox[3][3],
     const int dstbbox[3][3],
     const int regbbox[3][3]);
  void CCTK_FCALL CCTK_FNAME(prolongate_3d_real8_3tl_o3)
    (const CCTK_REAL8* src1, const CCTK_REAL8& t1,
     const CCTK_REAL8* src2, const CCTK_REAL8& t2,
     const CCTK_REAL8* src3, const CCTK_REAL8& t3,
     const int& srciext, const int& srcjext, const int& srckext,
     CCTK_REAL8* dst, const CCTK_REAL8& t,
     const int& dstiext, const int& dstjext, const int& dstkext,
     const int srcbbox[3][3],
     const int dstbbox[3][3],
     const int regbbox[3][3]);
  
}

template<>
void data<CCTK_REAL8,3>
::interpolate_from_innerloop (const vector<const generic_data<3>*> gsrcs,
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
  vector<const data*> srcs(gsrcs.size());
  for (int t=0; t<(int)srcs.size(); ++t) srcs[t] = (const data*)gsrcs[t];
  assert (srcs.size() == times.size() && srcs.size()>0);
  for (int t=0; t<(int)srcs.size(); ++t) {
    assert (srcs[t]->has_storage());
    assert (all(box.lower()>=srcs[t]->extent().lower()));
    assert (all(box.upper()<=srcs[t]->extent().upper()));
  }
  
  assert (proc() == srcs[0]->proc());
  
  int rank;
  MPI_Comm_rank (dist::comm, &rank);
  assert (rank == proc());
  
  const ibbox& sext = srcs[0]->extent();
  const ibbox& dext = extent();
  
  int srcshp[3], dstshp[3];
  int srcbbox[3][3], dstbbox[3][3], regbbox[3][3];
  
  for (int d=0; d<3; ++d) {
    srcshp[d] = (sext.shape() / sext.stride())[d];
    dstshp[d] = (dext.shape() / dext.stride())[d];
    
    srcbbox[0][d] = sext.lower()[d];
    srcbbox[1][d] = sext.upper()[d];
    srcbbox[2][d] = sext.stride()[d];
    
    dstbbox[0][d] = dext.lower()[d];
    dstbbox[1][d] = dext.upper()[d];
    dstbbox[2][d] = dext.stride()[d];
    
    regbbox[0][d] = box.lower()[d];
    regbbox[1][d] = box.upper()[d];
    regbbox[2][d] = box.stride()[d];
  }
  
  assert (all(dext.stride() == box.stride()));
  if (all(sext.stride() < dext.stride())) {
    
    assert (srcs.size() == 1);
    CCTK_FNAME(restrict_3d_real8)
      ((const CCTK_REAL8*)srcs[0]->storage(),
       srcshp[0], srcshp[1], srcshp[2],
       (CCTK_REAL8*)storage(),
       dstshp[0], dstshp[1], dstshp[2],
       srcbbox, dstbbox, regbbox);
    
  } else if (all(sext.stride() > dext.stride())) {
    
    switch (order_time) {
      
    case 0:
      assert (srcs.size()>=1);
      switch (order_space) {
      case 0:
      case 1:
	CCTK_FNAME(prolongate_3d_real8)
	  ((const CCTK_REAL8*)srcs[0]->storage(),
	   srcshp[0], srcshp[1], srcshp[2],
	   (CCTK_REAL8*)storage(),
	   dstshp[0], dstshp[1], dstshp[2],
	   srcbbox, dstbbox, regbbox);
	break;
      case 2:
      case 3:
	CCTK_FNAME(prolongate_3d_real8_o3)
	  ((const CCTK_REAL8*)srcs[0]->storage(),
	   srcshp[0], srcshp[1], srcshp[2],
	   (CCTK_REAL8*)storage(),
	   dstshp[0], dstshp[1], dstshp[2],
	   srcbbox, dstbbox, regbbox);
	break;
      default:
	assert (0);
      }
      break;
      
    case 1:
      assert (srcs.size()>=2);
      switch (order_space) {
      case 0:
      case 1:
	CCTK_FNAME(prolongate_3d_real8_2tl)
	  ((const CCTK_REAL8*)srcs[0]->storage(), times[0],
	   (const CCTK_REAL8*)srcs[1]->storage(), times[1],
	   srcshp[0], srcshp[1], srcshp[2],
	   (CCTK_REAL8*)storage(), time,
	   dstshp[0], dstshp[1], dstshp[2],
	   srcbbox, dstbbox, regbbox);
	break;
      case 2:
      case 3:
	CCTK_FNAME(prolongate_3d_real8_2tl_o3)
	  ((const CCTK_REAL8*)srcs[0]->storage(), times[0],
	   (const CCTK_REAL8*)srcs[1]->storage(), times[1],
	   srcshp[0], srcshp[1], srcshp[2],
	   (CCTK_REAL8*)storage(), time,
	   dstshp[0], dstshp[1], dstshp[2],
	   srcbbox, dstbbox, regbbox);
	break;
      default:
	assert (0);
      }
      break;
      
    case 2:
      assert (srcs.size()>=3);
      switch (order_space) {
      case 0:
      case 1:
	CCTK_FNAME(prolongate_3d_real8_3tl)
	  ((const CCTK_REAL8*)srcs[0]->storage(), times[0],
	   (const CCTK_REAL8*)srcs[1]->storage(), times[1],
	   (const CCTK_REAL8*)srcs[2]->storage(), times[2],
	   srcshp[0], srcshp[1], srcshp[2],
	   (CCTK_REAL8*)storage(), time,
	   dstshp[0], dstshp[1], dstshp[2],
	   srcbbox, dstbbox, regbbox);
	break;
      case 2:
      case 3:
	CCTK_FNAME(prolongate_3d_real8_3tl_o3)
	  ((const CCTK_REAL8*)srcs[0]->storage(), times[0],
	   (const CCTK_REAL8*)srcs[1]->storage(), times[1],
	   (const CCTK_REAL8*)srcs[2]->storage(), times[2],
	   srcshp[0], srcshp[1], srcshp[2],
	   (CCTK_REAL8*)storage(), time,
	   dstshp[0], dstshp[1], dstshp[2],
	   srcbbox, dstbbox, regbbox);
	break;
      default:
	assert (0);
      }
      break;
      
    default:
      assert (0);
    }
    
  } else {
    assert (0);
  }
}



// Output
template<class T,int D>
ostream& data<T,D>::output (ostream& os) const {
  os << "data<T," << D << ">:"
     << "extent=" << extent() << ","
     << "stride=" << stride() << ",size=" << size();
  return os;
}



#define INSTANTIATE(T)				\
template class data<T,3>;

#include "instantiate"

#undef INSTANTIATE
