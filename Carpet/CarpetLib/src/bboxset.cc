#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <set>
#include <stack>

#include "defs.hh"

#include "bboxset.hh"

using namespace std;



#define SKIP_NORMALIZE(s) skip_normalize_t skip_normalize_v(s)
// #define SKIP_NORMALIZE(s) do { } while(0)



// Constructors
template<typename T, int D>
bboxset<T,D>::bboxset ()
  : skip_normalize(false)
{
  assert (invariant());
}

template<typename T, int D>
bboxset<T,D>::bboxset (const box& b)
  : skip_normalize(false)
{
  //S if (not b.empty()) bs.insert(b);
  if (not b.empty()) bs.push_back(b);
  assert (invariant());
}

template<typename T, int D>
bboxset<T,D>::bboxset (const bboxset& s)
  : bs(s.bs), skip_normalize(false)
{
  assert (invariant());
}

template<typename T, int D>
bboxset<T,D>::bboxset (const list<box>& lb)
  : skip_normalize(false)
{
  SKIP_NORMALIZE(*this);
  for (typename list<box>::const_iterator
         li = lb.begin(); li != lb.end(); ++ li)
  {
    *this |= *li;
  }
}

template<typename T, int D>
bboxset<T,D>::bboxset (const vector<box>& lb)
  : skip_normalize(false)
{
  SKIP_NORMALIZE(*this);
  for (typename vector<box>::const_iterator
         vi = lb.begin(); vi != lb.end(); ++ vi)
  {
    *this |= *vi;
  }
}

template<typename T, int D>
bboxset<T,D>::bboxset (const vector<list<box> >& vlb)
  : skip_normalize(false)
{
  SKIP_NORMALIZE(*this);
  for (typename vector<list<box> >::const_iterator
         vli = vlb.begin(); vli != vlb.end(); ++ vli)
  {
    *this |= bboxset (*vli);
  }
}

template<typename T, int D>
template<typename U>
bboxset<T,D>::bboxset (const vector<U>& vb, const bbox<T,D> U::* const v)
  : skip_normalize(false)
{
  SKIP_NORMALIZE(*this);
  for (typename vector<U>::const_iterator
         vi = vb.begin(); vi != vb.end(); ++ vi)
  {
    *this |= (*vi).*v;
  }
}

template<typename T, int D>
template<typename U>
bboxset<T,D>::bboxset (const vector<U>& vb, const bboxset U::* const v)
  : skip_normalize(false)
{
  SKIP_NORMALIZE(*this);
  for (typename vector<U>::const_iterator
         vi = vb.begin(); vi != vb.end(); ++ vi)
  {
    *this |= (*vi).*v;
  }
}

template<typename T, int D>
bboxset<T,D> bboxset<T,D>::poison () {
  return bboxset (bbox<T,D>::poison());
}



// Invariant
template<typename T, int D>
bool bboxset<T,D>::invariant () const {
  // This is very slow when there are many bboxes
#if 0 && defined(CARPET_DEBUG)
  for (const_iterator bi=begin(); bi!=end(); ++bi) {
    if ((*bi).empty()) return false;
    if (not (*bi).is_aligned_with(*bs.begin())) return false;
    // check for overlap (quadratic -- expensive)
    for (const_iterator bi2=begin(); bi2!=bi; ++bi2) {
      if ((*bi2).intersects(*bi)) return false;
    }
  }
#endif
  return true;
}



// Normalisation
template<typename T, int D>
void bboxset<T,D>::normalize ()
{
  if (skip_normalize) return;
  assert (invariant());
  
  bboxset const oldbs = * this;
  size_type const oldsize = this->size();
  
  // Split all bboxes into small pieces which have all their
  // boundaries aligned.
  for (int d=0; d<D; ++d) {
    // Find all boundaries
    //S typedef set<T> buf;
    typedef vector<T> buf;
    buf sbnds;
    sbnds.reserve (2 * bs.size());
    for (typename bset::const_iterator si = bs.begin(); si != bs.end(); ++ si) {
      box const & b = * si;
      int const bstr = b.stride()[d];
      int const blo = b.lower()[d];
      int const bhi = b.upper()[d] + bstr;
      //S sbnds.insert (blo);
      //S sbnds.insert (bhi);
      sbnds.push_back (blo);
      sbnds.push_back (bhi);
    }
    sort (sbnds.begin(), sbnds.end());
    typename buf::iterator const last = unique (sbnds.begin(), sbnds.end());
    sbnds.resize (last - sbnds.begin());
    // Split bboxes
    bset nbs;
    for (typename bset::const_iterator si = bs.begin(); si != bs.end(); ++ si) {
      box const & b = * si;
      int const bstr = b.stride()[d];
      int const blo = b.lower()[d];
      int const bhi = b.upper()[d] + bstr;
      typename buf::const_iterator const ilo
        = lower_bound (sbnds.begin(), sbnds.end(), blo);
      typename buf::const_iterator const ihi
        = lower_bound (sbnds.begin(), sbnds.end(), bhi);
      assert (ilo != sbnds.end());
      assert (ihi != sbnds.end());
      assert (* ilo == blo);
      assert (* ihi == bhi);
      // Split one bbox
      for (typename buf::const_iterator curr = ilo; curr != ihi; ++ curr) {
        typename buf::const_iterator next = curr;
        advance (next, 1);
        int const nblo = * curr;
        int const nbhi = * next;
        assert (nbhi > nblo);   // ensure that the set remains sorted
        box const nb (b.lower().replace(d, nblo),
                      b.upper().replace(d, nbhi - bstr),
                      b.stride());
        //S nbs.insert (nb);
        nbs.push_back (nb);
      }
    }
    // Replace old set
    bs.clear ();
    bs.swap (nbs);
    assert (invariant());
  }
  
  // Combine bboxes if possible
  for (int d=0; d<D; ++d) {
    bset nbs;
    while (not bs.empty()) {
      typename bset::iterator si = bs.begin();
      assert (si != bs.end());
      
      box const b = * si;
      int const bstr = b.stride()[d];
      int const blo = b.lower()[d];
      int const bhi = b.upper()[d] + bstr;
      
      for (typename bset::iterator nsi = nbs.begin(); nsi != nbs.end(); ++ nsi)
      {
        box const nb = * nsi;
        int const nblo = nb.lower()[d];
        int const nbhi = nb.upper()[d] + bstr;
        
        box const mb (nb.lower().replace(d, blo),
                      nb.upper().replace(d, bhi - bstr),
                      nb.stride());
        
        // Check whether the other dimensions match
        if (b == mb) {
          // Check whether the bboxes are adjacent in this dimension
          if (nbhi == blo) {
            // Combine boxes, nb < b
            box const cb (b.lower().replace(d, nblo),
                          b.upper(),
                          b.stride());
            bs.erase (si);
            nbs.erase (nsi);
            //S bs.insert (cb);
            bs.push_back (cb);
            goto done;
          } else if (bhi == nblo) {
            // Combine boxes, b < nb
            box const cb (b.lower(),
                          b.upper().replace(d, nbhi - bstr),
                          b.stride());
            bs.erase (si);
            nbs.erase (nsi);
            //S bs.insert (cb);
            bs.push_back (cb);
            goto done;
          }
        }
      }
      bs.erase (si);
      //S nbs.insert (b);
      nbs.push_back (b);
    done:;
    }
    bs.swap (nbs);
    assert (invariant());
  }
  
  size_type const newsize = this->size();
  
  // Can't use operators on *this since these would call normalize again
  // assert (*this == oldbs);
  assert (newsize == oldsize);
}



// Accessors
// cost: O(n)
template<typename T, int D>
typename bboxset<T,D>::size_type bboxset<T,D>::size () const
{
  size_type s = 0;
  for (const_iterator bi=begin(); bi!=end(); ++bi) {
    const size_type bsz = (*bi).size();
    assert (numeric_limits<size_type>::max() - bsz >= s);
    s += bsz;
  }
  return s;
}




// Queries

// Intersection
// cost: O(n)
template<typename T, int D>
bool bboxset<T,D>::intersects (const box& b) const
{
  for (const_iterator bi=begin(); bi!=end(); ++bi) {
    if ((*bi).intersects(b)) return true;
  }
  return false;
}



// Add (bboxes that don't overlap)
// cost: O(1)
template<typename T, int D>
bboxset<T,D>& bboxset<T,D>::operator+= (const box& b)
{
  if (b.empty()) return *this;
  // This is very slow when there are many bboxes
#if 0 && defined(CARPET_DEBUG)
  // check for overlap
  for (const_iterator bi=begin(); bi!=end(); ++bi) {
    assert (not (*bi).intersects(b));
  }
#endif
  //S bs.insert(b);
  bs.push_back(b);
  assert (invariant());
  normalize();
  return *this;
}

// cost: O(n)
template<typename T, int D>
bboxset<T,D>& bboxset<T,D>::operator+= (const bboxset& s)
{
  SKIP_NORMALIZE(*this);
  for (const_iterator bi=s.begin(); bi!=s.end(); ++bi) {
    *this += *bi;
  }
  assert (invariant());
  return *this;
}

// cost: O(1)
template<typename T, int D>
bboxset<T,D>& bboxset<T,D>::add_transfer (bboxset& s)
{
  bs.splice (bs.end(), s.bs);
  assert (invariant());
  normalize();
  return *this;
}

// cost: O(n)
template<typename T, int D>
bboxset<T,D> bboxset<T,D>::operator+ (const box& b) const
{
  bboxset r(*this);
  r += b;
  assert (r.invariant());
  return r;
}

// cost: O(n)
template<typename T, int D>
bboxset<T,D> bboxset<T,D>::operator+ (const bboxset& s) const
{
  bboxset r(*this);
  r += s;
  assert (r.invariant());
  return r;
}



// Union
// cost: O(n)
template<typename T, int D>
bboxset<T,D>& bboxset<T,D>::operator|= (const box& b)
{
#if 0
  // this has a cost of O(n^2)
  bboxset tmp = b - *this;
  add_transfer (tmp);
#else
  // this has a cost of O(n)
  bset oldbs;
  oldbs.swap(bs);
  bs.push_back(b);
  SKIP_NORMALIZE(*this);
  for (const_iterator bi=oldbs.begin(); bi!=oldbs.end(); ++bi) {
    bboxset tmp = *bi - b;
    add_transfer(tmp);
  }
#endif
  assert (invariant());
  return *this;
}

// cost: O(n^2)
template<typename T, int D>
bboxset<T,D>& bboxset<T,D>::operator|= (const bboxset& s)
{
  bboxset tmp = s - *this;
  add_transfer (tmp);
  assert (invariant());
  return *this;
}

// cost: O(n)
template<typename T, int D>
bboxset<T,D> bboxset<T,D>::operator| (const box& b) const
{
  bboxset r(*this);
  r |= b;
  assert (r.invariant());
  return r;
}

// cost: O(n^2)
template<typename T, int D>
bboxset<T,D> bboxset<T,D>::operator| (const bboxset& s) const
{
  bboxset r(*this);
  r |= s;
  assert (r.invariant());
  return r;
}



// Intersection
// cost: O(n)
template<typename T, int D>
bboxset<T,D> bboxset<T,D>::operator& (const box& b) const
{
  // start with an empty set
  bboxset r;
  {
    SKIP_NORMALIZE(r);
    // walk all my elements
    for (const_iterator bi=begin(); bi!=end(); ++bi) {
      // insert the intersection with the bbox
      r += *bi & b;
    }
  }
  assert (r.invariant());
  return r;
}

// cost: O(n)
template<typename T, int D>
bboxset<T,D> bboxset<T,D>::operator& (const bboxset& s) const
{
  // start with an empty set
  bboxset r;
  {
    SKIP_NORMALIZE(r);
    // walk all the bboxes
    for (const_iterator bi=s.begin(); bi!=s.end(); ++bi) {
      // insert the intersection with this bbox
      bboxset tmp = *this & *bi;
      r.add_transfer (tmp);
    }
  }
  assert (r.invariant());
  return r;
}

// cost: O(n)
template<typename T, int D>
bboxset<T,D>& bboxset<T,D>::operator&= (const box& b)
{
  *this = *this & b;
  assert (invariant());
  return *this;
}

// cost: O(n)
template<typename T, int D>
bboxset<T,D>& bboxset<T,D>::operator&= (const bboxset& s)
{
  *this = *this & s;
  assert (invariant());
  return *this;
}



// Difference
// cost: O(1)
template<typename T, int D>
bboxset<T,D> bboxset<T,D>::minus (const bbox<T,D>& b1, const bbox<T,D>& b2)
{
  assert (b1.is_aligned_with(b2));
  if (b1.empty()) return bboxset<T,D>();
  if (not b1.intersects(b2)) return bboxset<T,D>(b1);
  vect<T,D> const b2lo = max (b1.lower(), b2.lower());
  vect<T,D> const b2up = min (b1.upper(), b2.upper());
  vect<T,D> const & b1lo = b1.lower();
  vect<T,D> const & b1up = b1.upper();
  vect<T,D> const & str = b1.stride();
  bboxset<T,D> r;
  SKIP_NORMALIZE(r);
  {
    for (int d=0; d<D; ++d) {
      // make resulting bboxes as large as possible in x-direction
      // (for better consumption by Fortranly ordered arrays)
      vect<T,D> lb, ub;
      bbox<T,D> b;
      for (int dd=0; dd<D; ++dd) {
        if (dd<d) {
          lb[dd] = b2lo[dd];
          ub[dd] = b2up[dd];
        } else if (dd>d) {
          lb[dd] = b1lo[dd];
          ub[dd] = b1up[dd];
        }
      }
      lb[d] = b1lo[d];
      ub[d] = b2lo[d] - str[d];
      b = bbox<T,D>(lb,ub,str);
      r += b;
      lb[d] = b2up[d] + str[d];
      ub[d] = b1up[d];
      b = bbox<T,D>(lb,ub,str);
      r += b;
    }
  }
  assert (r.invariant());
  return r;
}

// cost: O(n)
template<typename T, int D>
bboxset<T,D> bboxset<T,D>::operator- (const box& b) const
{
  // start with an empty set
  bboxset r;
  {
    SKIP_NORMALIZE(r);
    // walk all my elements
    for (const_iterator bi=begin(); bi!=end(); ++bi) {
      // insert the difference with the bbox
      bboxset tmp = *bi - b;
      r.add_transfer (tmp);
    }
  }
  assert (r.invariant());
  return r;
}
  
// cost: O(n^2)
template<typename T, int D>
bboxset<T,D>& bboxset<T,D>::operator-= (const bboxset& s)
{
  SKIP_NORMALIZE(*this);
  for (const_iterator bi=s.begin(); bi!=s.end(); ++bi) {
    *this -= *bi;
  }
  assert (invariant());
  return *this;
}

// cost: O(n^2)
template<typename T, int D>
bboxset<T,D> bboxset<T,D>::operator- (const bboxset& s) const
{
  bboxset r(*this);
  r -= s;
  assert (r.invariant());
  return r;
}

// cost: O(n^2)
template<typename T, int D>
bboxset<T,D> bboxset<T,D>::minus (const bbox<T,D>& b, const bboxset<T,D>& s)
{
  bboxset<T,D> r = bboxset<T,D>(b) - s;
  assert (r.invariant());
  return r;
}



// cost: O(n)
template<typename T, int D>
typename bboxset<T,D>::box bboxset<T,D>::container () const
{
  box b;
  for (const_iterator bi=begin(); bi!=end(); ++bi) {
    b = b.expanded_containing(*bi);
  }
  return b;
}

template<typename T, int D>
bboxset<T,D> bboxset<T,D>::pseudo_inverse (const int n) const
{
  assert (not empty());
  box const c = container().expand(n,n);
  return c - *this;
}

// cost: O(n^2) in general, but only O(n) for shifting
template<typename T, int D>
bboxset<T,D> bboxset<T,D>::expand (const vect<T,D>& lo, const vect<T,D>& hi)
  const
{
  bboxset res;
  {
    SKIP_NORMALIZE(res);
    if (all (lo == -hi)) {
      // Special case for shifting, since this is faster
      for (const_iterator bi=begin(); bi!=end(); ++bi) {
        res += (*bi).expand(lo,hi);
      }
    } else {
      // We don't know (yet?) how to shrink a set
      assert (all (lo>=0 and hi>=0));
      for (const_iterator bi=begin(); bi!=end(); ++bi) {
        res |= (*bi).expand(lo,hi);
      }
    }
  }
  return res;
}

// cost: O(n^2) in general, but only O(n) for shifting
template<typename T, int D>
bboxset<T,D> bboxset<T,D>::expand (const vect<T,D>& lo, const vect<T,D>& hi,
                                   const vect<T,D>& denom) const
{
  assert (all(denom > vect<T,D>(0)));
  bboxset res;
  {
    SKIP_NORMALIZE(res);
    if (all (lo == -hi)) {
      // Special case for shifting, since this is faster
      for (const_iterator bi=begin(); bi!=end(); ++bi) {
        res += (*bi).expand(lo,hi,denom);
      }
    } else {
      // We don't know (yet?) how to shrink a set
      assert (all ((lo>=0 and hi>=0) or (lo == hi)));
      for (const_iterator bi=begin(); bi!=end(); ++bi) {
        res |= (*bi).expand(lo,hi,denom);
      }
    }
  }
  return res;
}

template<typename T, int D>
bboxset<T,D> bboxset<T,D>::expanded_for (const box& b) const
{
  bboxset res;
  {
    SKIP_NORMALIZE(res);
    for (const_iterator bi=begin(); bi!=end(); ++bi) {
      res |= (*bi).expanded_for(b);
    }
  }
  return res;
}

#warning "TODO: this is incorrect"
#if 1
template<typename T, int D>
bboxset<T,D> bboxset<T,D>::contracted_for (const box& b) const
{
  bboxset res;
  {
    SKIP_NORMALIZE(res);
    for (const_iterator bi=begin(); bi!=end(); ++bi) {
      res |= (*bi).contracted_for(b);
    }
  }
  return res;
}
#endif



// Equality
template<typename T, int D>
bool bboxset<T,D>::operator<= (const bboxset<T,D>& s) const
{
  return (*this - s).empty();
}

template<typename T, int D>
bool bboxset<T,D>::operator< (const bboxset<T,D>& s) const
{
  return (*this - s).empty() && not (s - *this).empty();
}

template<typename T, int D>
bool bboxset<T,D>::operator>= (const bboxset<T,D>& s) const
{
  return s <= *this;
}

template<typename T, int D>
bool bboxset<T,D>::operator> (const bboxset<T,D>& s) const
{
  return s < *this;
}

template<typename T, int D>
bool bboxset<T,D>::operator== (const bboxset<T,D>& s) const
{
  return (*this <= s) && (*this >= s);
}

template<typename T, int D>
bool bboxset<T,D>::operator!= (const bboxset<T,D>& s) const
{
  return not (*this == s);
}



// Input
template<typename T,int D>
istream& bboxset<T,D>::input (istream& is)
{
  T Tdummy;
  try {
    skipws (is);
    consume (is, "bboxset<");
    consume (is, typestring(Tdummy));
    consume (is, ",");
    int D_;
    is >> D_;
    if (D_ != D) {
      cout << "Input error: Wrong bboxset dimension " << D_ << ", expected " << D << endl;
      throw input_error();
    }
    consume (is, ">:{");
    consume (is, "size=");
    size_type size_;
    is >> size_;
    consume (is, ",");
    consume (is, "setsize=");
    int setsize_;
    is >> setsize_;
    consume (is, ",");
    consume (is, "set=");
    is >> bs;
    consume (is, "}");
  } catch (input_error & err) {
    cout << "Input error while reading a bboxset<" << typestring(Tdummy) << "," << D << ">" << endl;
    throw err;
  }
  normalize();
  return is;
}



// Output
template<typename T,int D>
ostream& bboxset<T,D>::output (ostream& os) const
{
  T Tdummy;
  os << "bboxset<" << typestring(Tdummy) << "," << D << ">:{"
     << "size=" << size() << ","
     << "setsize=" << setsize() << ","
     << "set=" << bs
     << "}";
  return os;
}



template class bboxset<int,dim>;
template size_t memoryof (const bboxset<int,dim>& s);
template istream& operator>> (istream& is, bboxset<int,dim>& s);
template ostream& operator<< (ostream& os, const bboxset<int,dim>& s);

#include "dh.hh"
#include "region.hh"

template bboxset<int,dim>::bboxset (const vector<dh::full_dboxes>& vb, const bbox<int,dim> dh::full_dboxes::* const v);
template bboxset<int,dim>::bboxset (const vector<dh::full_dboxes>& vb, const bboxset<int,dim> dh::full_dboxes::* const v);
template bboxset<int,dim>::bboxset (const vector<region_t>& vb, const bbox<int,dim> region_t::* const v);
