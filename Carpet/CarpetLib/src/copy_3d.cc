#include <cctk.h>
#include <cctk_Parameters.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "gdata.hh"
#include "operator_prototypes_3d.hh"
#include "typeprops.hh"

using namespace std;



namespace CarpetLib {


  
#define SRCIND3(i,j,k)                                  \
  index3 (srcioff + (i), srcjoff + (j), srckoff + (k),  \
          srciext, srcjext, srckext)
#define DSTIND3(i,j,k)                                  \
  index3 (dstioff + (i), dstjoff + (j), dstkoff + (k),  \
          dstiext, dstjext, dstkext)
  
  
  
  template <typename T>
  void
  copy_3d (T const * restrict const src,
           ivect3 const & restrict srcext,
           T * restrict const dst,
           ivect3 const & restrict dstext,
           ibbox3 const & restrict srcbbox,
           ibbox3 const & restrict dstbbox,
           ibbox3 const & restrict srcregbbox1,
           ibbox3 const & restrict dstregbbox,
           void * extraargs)
  {
    islab const * restrict const slabinfo =
      static_cast<islab const *> (extraargs);
    
    if (any (srcbbox.stride() != dstregbbox.stride() or
             dstbbox.stride() != dstregbbox.stride() or
             srcregbbox1.stride() != dstregbbox.stride()))
    {
#pragma omp critical
      {
        cout << "copy_3d.cc:" << endl
             << "srcbbox=" << srcbbox << endl
             << "dstbbox=" << dstbbox << endl
             << "srcregbbox=" << srcregbbox1 << endl
             << "dstregbbox=" << dstregbbox << endl;
        CCTK_WARN (0, "Internal error: strides disagree");
      }
    }
    
    if (any (srcbbox.stride() != dstbbox.stride())) {
      CCTK_WARN (0, "Internal error: strides disagree");
    }
    
    // This could be handled, but is likely to point to an error
    // elsewhere
    if (dstregbbox.empty()) {
#pragma omp critical
      CCTK_WARN (0, "Internal error: region extent is empty");
    }
    
    ibbox const srcregbbox =
      slabinfo ? dstregbbox.shift(slabinfo->offset) : dstregbbox;
    assert (srcregbbox.is_contained_in(srcregbbox1));
    
    assert(all(srcregbbox.shape() == dstregbbox.shape()));
    
    if (not srcregbbox.is_contained_in(srcbbox) or
        not dstregbbox.is_contained_in(dstbbox))
    {
#pragma omp critical
      {
        cout << "copy_3d.cc:" << endl
             << "srcbbox=" << srcbbox << endl
             << "dstbbox=" << dstbbox << endl
             << "srcregbbox=" << srcregbbox << endl
             << "dstregbbox=" << dstregbbox << endl;
        if (slabinfo) {
          cout << "slabinfo=" << *slabinfo << endl;
        }
        CCTK_WARN (0, "Internal error: region extent is not contained in array extent");
      }
    }
    
    if (any (srcext != gdata::allocated_memory_shape(srcbbox.shape() / srcbbox.stride()) or
             dstext != gdata::allocated_memory_shape(dstbbox.shape() / dstbbox.stride())))
    {
#pragma omp critical
      CCTK_WARN (0, "Internal error: array sizes don't agree with bounding boxes");
    }
    
    
    
    ivect3 const regext = dstregbbox.shape() / dstregbbox.stride();
    assert (all ((srcregbbox.lower() - srcbbox.lower()) % srcbbox.stride() == 0));
    ivect3 const srcoff = (srcregbbox.lower() - srcbbox.lower()) / srcbbox.stride();
    assert (all ((dstregbbox.lower() - dstbbox.lower()) % dstbbox.stride() == 0));
    ivect3 const dstoff = (dstregbbox.lower() - dstbbox.lower()) / dstbbox.stride();
    
    
    
    ptrdiff_t const srciext = srcext[0];
    ptrdiff_t const srcjext = srcext[1];
    ptrdiff_t const srckext = srcext[2];
    
    ptrdiff_t const dstiext = dstext[0];
    ptrdiff_t const dstjext = dstext[1];
    ptrdiff_t const dstkext = dstext[2];
    
    ptrdiff_t const regiext = regext[0];
    ptrdiff_t const regjext = regext[1];
    ptrdiff_t const regkext = regext[2];
    
    ptrdiff_t const srcioff = srcoff[0];
    ptrdiff_t const srcjoff = srcoff[1];
    ptrdiff_t const srckoff = srcoff[2];
    
    ptrdiff_t const dstioff = dstoff[0];
    ptrdiff_t const dstjoff = dstoff[1];
    ptrdiff_t const dstkoff = dstoff[2];
    
    
    
    // Loop over region
#pragma omp parallel for collapse(3)
    for (int k=0; k<regkext; ++k) {
      for (int j=0; j<regjext; ++j) {
        for (int i=0; i<regiext; ++i) {
          
          dst [DSTIND3(i, j, k)] = src [SRCIND3(i, j, k)];
          
        }
      }
    }
    
  }
  
  
  
#define TYPECASE(N,T)                           \
  template                                      \
  void                                          \
  copy_3d (T const * restrict const src,        \
           ivect3 const & restrict srcext,      \
           T * restrict const dst,              \
           ivect3 const & restrict dstext,      \
           ibbox3 const & restrict srcbbox,     \
           ibbox3 const & restrict dstbbox,     \
           ibbox3 const & restrict srcregbbox,  \
           ibbox3 const & restrict dstregbbox,  \
           void * extraargs);
#include "typecase.hh"
#undef TYPECASE
  
  
  
} // namespace CarpetLib
