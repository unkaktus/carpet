/***************************************************************************
                          data.hh  -  Data storage
                             -------------------
    begin                : Sun Jun 11 2000
    copyright            : (C) 2000 by Erik Schnetter
    email                : schnetter@astro.psu.edu

    $Header: /home/eschnett/C/carpet/Carpet/Carpet/CarpetLib/src/data.hh,v 1.11 2002/09/25 15:49:15 schnetter Exp $

 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef DATA_HH
#define DATA_HH

#include <assert.h>

#include <iostream>
#include <string>

#include "cctk.h"

#include "defs.hh"
#include "dist.hh"
#include "bbox.hh"
#include "gdata.hh"
#include "vect.hh"

using namespace std;



// A real data storage
template<class T,int D>
class data: public generic_data<D> {
  
  // Types
  typedef vect<int,D> ivect;
  typedef bbox<int,D> ibbox;

  // Fields
  T* _storage;			// the data (if located on this processor)

public:
  
  // Constructors
  data ();
  data (const ibbox& extent, const int proc);

  // Destructors
  virtual ~data ();

  // Pseudo constructors
  virtual data* make_typed () const;

  // Storage management
  virtual void allocate (const ibbox& extent, const int proc,
			 void* const mem=0);
  virtual void free ();
  virtual void transfer_from (generic_data<D>* gsrc);

  // Processor management
  virtual void change_processor (const int newproc, void* const mem=0);

  // Accessors
  virtual const void* storage () const {
    assert (_has_storage);
    return _storage;
  }

  virtual void* storage () {
    assert (_has_storage);
    return _storage;
  }
  
  // Data accessors
  const T& operator[] (const ivect& index) const {
    assert (_storage);
    return _storage[offset(index)];
  }
  
  T& operator[] (const ivect& index) {
    assert (_storage);
    return _storage[offset(index)];
  }
  
  // Data manipulators
  void copy_from_innerloop (const generic_data<D>* gsrc,
			    const ibbox& box);
  void interpolate_from_innerloop (const vector<const generic_data<D>*> gsrcs,
				   const vector<CCTK_REAL> times,
				   const ibbox& box, const CCTK_REAL time,
				   const int order_space,
				   const int order_time);
  
public:

  // Output
  ostream& output (ostream& os) const;
};



#endif // DATA_HH
