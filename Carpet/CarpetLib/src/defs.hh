#ifndef DEFS_HH
#define DEFS_HH

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <algorithm>
#include <cassert>
#if 0
#include <complex>
#endif
#include <iostream>
#include <list>
#include <set>
#include <stack>
#include <vector>

#include "cctk.h"

using namespace std;

// A general type
enum centering { vertex_centered, cell_centered };

// Useful helper
template<class T>
inline T square (const T& x) { return x*x; }

// Another useful helper
template<class T>
T ipow (T x, int y);



// Input streams
struct input_error { };
void skipws (istream& is);
void expect (istream& is, const char c);
void consume (istream& is, const char c);



// Names for types

#if 0

inline const char * typestring (const char& dummy)
{ return "char"; }

inline const char * typestring (const signed char& dummy)
{ return "signed char"; }

inline const char * typestring (const unsigned char& dummy)
{ return "unsigned char"; }

inline const char * typestring (const short& dummy)
{ return "short"; }

inline const char * typestring (const unsigned short& dummy)
{ return "unsigned short"; }

inline const char * typestring (const int& dummy)
{ return "int"; }

inline const char * typestring (const unsigned int& dummy)
{ return "unsigned int"; }

inline const char * typestring (const long& dummy)
{ return "long"; }

inline const char * typestring (const unsigned long& dummy)
{ return "unsigned long"; }

inline const char * typestring (const long long& dummy)
{ return "long long"; }

inline const char * typestring (const unsigned long long& dummy)
{ return "unsigned long long"; }

inline const char * typestring (const float& dummy)
{ return "float"; }

inline const char * typestring (const double& dummy)
{ return "double"; }

inline const char * typestring (const long double& dummy)
{ return "long double"; }

inline const char * typestring (const complex<float>& dummy)
{ return "complex<float>"; }

inline const char * typestring (const complex<double>& dummy)
{ return "complex<double>"; }

inline const char * typestring (const complex<long double>& dummy)
{ return "complex<long double>"; }

#else

#  ifdef CCTK_INT1
inline const char * typestring (const CCTK_INT1& dummy)
{ return "CCTK_INT1"; }
#  endif

#  ifdef CCTK_INT2
inline const char * typestring (const CCTK_INT2& dummy)
{ return "CCTK_INT2"; }
#  endif

#  ifdef CCTK_INT4
inline const char * typestring (const CCTK_INT4& dummy)
{ return "CCTK_INT4"; }
#  endif

#  ifdef CCTK_INT8
inline const char * typestring (const CCTK_INT8& dummy)
{ return "CCTK_INT8"; }
#  endif

#  ifdef CCTK_REAL4
inline const char * typestring (const CCTK_REAL4& dummy)
{ return "CCTK_REAL4"; }
#  endif

#  ifdef CCTK_REAL8
inline const char * typestring (const CCTK_REAL8& dummy)
{ return "CCTK_REAL8"; }
#  endif

#  ifdef CCTK_REAL16
inline const char * typestring (const CCTK_REAL16& dummy)
{ return "CCTK_REAL16"; }
#  endif

#  ifdef CCTK_REAL4
inline const char * typestring (const CCTK_COMPLEX8& dummy)
{ return "CCTK_COMPLEX8"; }
#  endif

#  ifdef CCTK_REAL8
inline const char * typestring (const CCTK_COMPLEX16& dummy)
{ return "CCTK_COMPLEX16"; }
#  endif

#  ifdef CCTK_REAL16
inline const char * typestring (const CCTK_COMPLEX32& dummy)
{ return "CCTK_COMPLEX32"; }
#  endif

#endif



// Container input
template<class T> istream& input (istream& is, vector<T>& v);

template<class T>
inline istream& operator>> (istream& is, vector<T>& v) {
  return input(is,v);
}



// Container output
template<class T> ostream& output (ostream& os, const list<T>& l);
template<class T> ostream& output (ostream& os, const set<T>& s);
template<class T> ostream& output (ostream& os, const stack<T>& s);
template<class T> ostream& output (ostream& os, const vector<T>& v);

template<class T>
inline ostream& operator<< (ostream& os, const list<T>& l) {
  return output(os,l);
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
