#ifndef DEFS_HH
#define DEFS_HH

#include <cctk.h>

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <stack>
#include <vector>

#include "typeprops.hh"



using namespace std;



// Stringify
#define STRINGIFY1(x) #x
#define STRINGIFY(x) STRINGIFY1(x)



// Structure member offsets
#undef offsetof
#define offsetof(TYPE,MEMBER) ((size_t)&((TYPE*)0)->MEMBER)
#undef __offsetof__
#define __offsetof__ offsetof



// Number of dimensions
#ifndef CARPET_DIM
#  define CARPET_DIM 3
#endif
const int dim = CARPET_DIM;



// Begin a new line without flushing the output buffer
char const * const eol = "\n";


  
// A compile time pseudo assert statement
#define static_assert(_x, _msg) do { typedef int ai[(_x) ? 1 : -1]; } while(0)



// Check a return value
#define check(_expr) do { bool const _val = (_expr); assert(_val); } while(0)



// Use this macro AT instead of vector's operator[] or at().
// Depending on the macro CARPET_OPTIMISE, this macro AT either checks
// for valid indices or not.
#if ! defined(CARPET_OPTIMISE)
#  define AT(index) at(index)
#else
#  define AT(index) operator[](index)
#endif
  


// Some shortcuts for type names
template<typename T, int D> class vect;
template<typename T, int D> class bbox;
template<typename T, int D> class bboxset;
template<typename T, int D, typename P> class fulltree;

typedef vect<bool,dim>      bvect;
typedef vect<int,dim>       ivect;
typedef vect<CCTK_INT,dim>  jvect;
typedef vect<CCTK_REAL,dim> rvect;
typedef bbox<int,dim>       ibbox;
typedef bbox<CCTK_INT,dim>  jbbox;
typedef bbox<CCTK_REAL,dim> rbbox;
typedef bboxset<int,dim>    ibset;
  
// (Try to replace these by b2vect and i2vect)
typedef vect<vect<bool,2>,dim> bbvect;
typedef vect<vect<int,2>,dim>  iivect;
typedef vect<vect<CCTK_INT,2>,dim> jjvect;

typedef vect<vect<bool,dim>,2> b2vect;
typedef vect<vect<int,dim>,2>  i2vect;
typedef vect<vect<CCTK_INT,dim>,2>  j2vect;
typedef vect<vect<CCTK_REAL,dim>,2> r2vect;



struct pseudoregion_t;
struct region_t;

typedef fulltree<int,dim,pseudoregion_t> ipfulltree;



// A general type
enum centering { error_centered, vertex_centered, cell_centered };



// Useful helper
template<class T>
inline T square (const T x) { return x*x; }

// Another useful helper
template<class T>
T ipow (T x, int y) CCTK_ATTRIBUTE_CONST;



// Access to CarpetLib parameters
CCTK_INT get_poison_value() CCTK_ATTRIBUTE_PURE;
CCTK_INT get_deadbeef() CCTK_ATTRIBUTE_PURE;



// Input streams
struct input_error { };
void skipws (istream& is);
void expect (istream& is, char c);
void consume (istream& is, char c);
void consume (istream& is, char const * c);



// Names for types

#ifdef HAVE_CCTK_INT1
inline const char * typestring (const CCTK_INT1&)
{ return "CCTK_INT1"; }
#endif

#ifdef HAVE_CCTK_INT2
inline const char * typestring (const CCTK_INT2&)
{ return "CCTK_INT2"; }
#endif

#ifdef HAVE_CCTK_INT4
inline const char * typestring (const CCTK_INT4&)
{ return "CCTK_INT4"; }
#endif

#ifdef HAVE_CCTK_INT8
inline const char * typestring (const CCTK_INT8&)
{ return "CCTK_INT8"; }
#endif

#ifdef HAVE_CCTK_REAL4
inline const char * typestring (const CCTK_REAL4&)
{ return "CCTK_REAL4"; }
#endif

#ifdef HAVE_CCTK_REAL8
inline const char * typestring (const CCTK_REAL8&)
{ return "CCTK_REAL8"; }
#endif

#ifdef HAVE_CCTK_REAL16
inline const char * typestring (const CCTK_REAL16&)
{ return "CCTK_REAL16"; }
#endif

#ifdef HAVE_CCTK_REAL4
inline const char * typestring (const CCTK_COMPLEX8&)
{ return "CCTK_COMPLEX8"; }
#endif

#ifdef HAVE_CCTK_REAL8
inline const char * typestring (const CCTK_COMPLEX16&)
{ return "CCTK_COMPLEX16"; }
#endif

#ifdef HAVE_CCTK_REAL16
inline const char * typestring (const CCTK_COMPLEX32&)
{ return "CCTK_COMPLEX32"; }
#endif


  
// Provide implementations for some functions for complex numbers

#define IMPLEMENT_FUNCTIONS(T)                          \
                                                        \
  inline int good_isfinite(T const& x)                  \
  {                                                     \
    return isfinite(x.real()) and isfinite(x.imag());   \
  }                                                     \
                                                        \
  inline int good_isinf(T const& x)                     \
  {                                                     \
    return isinf(x.real()) or isinf(x.imag());          \
  }                                                     \
                                                        \
  inline int good_isnan(T const& x)                     \
  {                                                     \
    return isnan(x.real()) or isnan(x.imag());          \
  }                                                     \
                                                        \
  inline int good_isnormal(T const& x)                  \
  {                                                     \
    return isnormal(x.real()) or isnormal(x.imag());    \
  }

namespace std {
  namespace Cactus {
  
#ifdef HAVE_CCTK_COMPLEX8
  IMPLEMENT_FUNCTIONS(CCTK_COMPLEX8)
#endif
#ifdef HAVE_CCTK_COMPLEX16
  IMPLEMENT_FUNCTIONS(CCTK_COMPLEX16)
#endif
#ifdef HAVE_CCTK_COMPLEX32
  IMPLEMENT_FUNCTIONS(CCTK_COMPLEX32)
#endif
    
  }
}

#undef IMPLEMENT_FUNCTIONS



// Container equality
template <typename T>
bool equals (vector<T> const& v, vector<T> const& w)
{
  if (v.size() != w.size()) return false;
  for (size_t i=0; i<v.size(); ++i) {
    if (v.AT(i) != w.AT(i)) return false;
  }
  return true;
}



// Container memory usage
inline size_t memoryof (char const & e) { return sizeof e; }
inline size_t memoryof (short const & e) { return sizeof e; }
inline size_t memoryof (int const & e) { return sizeof e; }
inline size_t memoryof (long const & e) { return sizeof e; }
inline size_t memoryof (long long const & e) { return sizeof e; }
inline size_t memoryof (unsigned char const & e) { return sizeof e; }
inline size_t memoryof (unsigned short const & e) { return sizeof e; }
inline size_t memoryof (unsigned int const & e) { return sizeof e; }
inline size_t memoryof (unsigned long const & e) { return sizeof e; }
inline size_t memoryof (unsigned long long const & e) { return sizeof e; }
inline size_t memoryof (float const & e) { return sizeof e; }
inline size_t memoryof (double const & e) { return sizeof e; }
inline size_t memoryof (long double const & e) { return sizeof e; }
inline size_t memoryof (void const * const & e) { return sizeof e; }
template<class T> inline size_t memoryof (T const * const & e) { return sizeof e; }
template<class T> inline size_t memoryof (typename list<T>::iterator const & i) { return sizeof i; }
template<class T> inline size_t memoryof (typename list<T>::const_iterator const & i) { return sizeof i; }

template<class T> size_t memoryof (list<T> const & c) CCTK_ATTRIBUTE_PURE;
template<class T> size_t memoryof (set<T> const & c) CCTK_ATTRIBUTE_PURE;
template<class T> size_t memoryof (stack<T> const & c) CCTK_ATTRIBUTE_PURE;
template<class T> size_t memoryof (vector<T> const & c) CCTK_ATTRIBUTE_PURE;



// Container input
template<class T> istream& input (istream& is, list<T>& l);
template<class T> istream& input (istream& is, set<T>& s);
template<class T> istream& input (istream& is, vector<T>& v);

template<class T>
inline istream& operator>> (istream& is, list<T>& l) {
  return input(is,l);
}

template<class T>
inline istream& operator>> (istream& is, set<T>& s) {
  return input(is,s);
}

template<class T>
inline istream& operator>> (istream& is, vector<T>& v) {
  return input(is,v);
}



// Container output
template<class T> ostream& output (ostream& os, const list<T>& l);
template<class S, class T> ostream& output (ostream& os, const map<S,T>& m);
template<class S, class T> ostream& output (ostream& os, const pair<S,T>& p);
template<class T> ostream& output (ostream& os, const set<T>& s);
template<class T> ostream& output (ostream& os, const stack<T>& s);
template<class T> ostream& output (ostream& os, const vector<T>& v);

template<class T>
inline ostream& operator<< (ostream& os, const list<T>& l) {
  return output(os,l);
}

template<class S, class T>
inline ostream& operator<< (ostream& os, const map<S,T>& m) {
  return output(os,m);
}

template<class T>
inline ostream& operator<< (ostream& os, const set<T>& s) {
  return output(os,s);
}

template<class T>
inline ostream& operator<< (ostream& os, const stack<T>& s) {
  return output(os,s);
}

template<class T>
inline ostream& operator<< (ostream& os, const vector<T>& v) {
  return output(os,v);
}



#endif // DEFS_HH
