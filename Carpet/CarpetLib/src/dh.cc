// $Header: /home/cvs/carpet/Carpet/CarpetLib/src/dh.cc,v 1.57 2004/09/17 16:37:26 schnetter Exp $

#include <assert.h>

#include "cctk.h"
#include "cctk_Parameters.h"

#include "defs.hh"
#include "dist.hh"
#include "ggf.hh"
#include "vect.hh"

#include "dh.hh"

using namespace std;



// Constructors
template<int D>
dh<D>::dh (gh<D>& h,
           const ivect& lghosts, const ivect& ughosts,
           const int prolongation_order_space, const int buffer_width)
  : h(h),
    lghosts(lghosts), ughosts(ughosts),
    prolongation_order_space(prolongation_order_space),
    buffer_width(buffer_width)
{
  assert (all(lghosts>=0 && ughosts>=0));
  assert (prolongation_order_space>=0);
  assert (buffer_width>=0);
  h.add(this);
  CHECKPOINT;
  recompose (false);
}

// Destructors
template<int D>
dh<D>::~dh ()
{
  CHECKPOINT;
  h.remove(this);
}

// Helpers
template<int D>
int dh<D>::prolongation_stencil_size () const {
  assert (prolongation_order_space>=0);
  return prolongation_order_space/2;
}

// Modifiers
template<int D>
void dh<D>::recompose (const bool do_prolongate) {
  DECLARE_CCTK_PARAMETERS;
  
  CHECKPOINT;

  boxes.clear();

  allocate_bboxes();
  
  foreach_reflevel_component_mglevel (&dh<D>::setup_sync_and_refine_boxes );
  foreach_reflevel_component_mglevel (&dh<D>::intersect_sync_with_interior );
  foreach_reflevel_component_mglevel (&dh<D>::setup_multigrid_boxes );
  foreach_reflevel_component_mglevel (&dh<D>::setup_refinement_interior_boxes );
  foreach_reflevel_component_mglevel (&dh<D>::setup_refinement_exterior_boxes );
  foreach_reflevel_component_mglevel (&dh<D>::setup_restrict_interior_boxes );
  foreach_reflevel_component_mglevel (&dh<D>::trim_unsynced_boundaries );

  calculate_bases();

  if (output_bboxes) {
    cout << endl << h << endl;
    foreach_reflevel_component_mglevel (&dh<D>::output_bboxes );
    output_bases();
  }
  
  foreach_reflevel_component_mglevel (&dh<D>::assert_assert_assert );

  if (! save_memory_during_regridding) {
    save_time( do_prolongate);
  } else {
    save_memory( do_prolongate);
  }
}

template<int D>
void dh<D>::allocate_bboxes() {
  boxes.resize(h.reflevels());
  for (int rl=0; rl<h.reflevels(); ++rl) {
    boxes.at(rl).resize(h.components(rl));
    for (int c=0; c<h.components(rl); ++c) {
      boxes.at(rl).at(c).resize(h.mglevels(rl,c));
      for (int ml=0; ml<h.mglevels(rl,c); ++ml) {
        const ibbox intr = h.extents.at(rl).at(c).at(ml);
        dboxes & b = boxes.at(rl).at(c).at(ml);

        // Interior
        // (the interior of the grid has the extent as specified by
        // the user)
        b.interior = intr;

        // Exterior (add ghost zones)
        // (the content of the exterior is completely determined by
        // the interior of this or other components; the content of
        // the exterior is redundant)
        ivect ldist(lghosts), udist(ughosts);
        for (int d=0; d<D; ++d) {
          if (h.outer_boundaries.at(rl).at(c)[d][0]) ldist[d] = 0;
          if (h.outer_boundaries.at(rl).at(c)[d][1]) udist[d] = 0;
        }
        b.exterior = intr.expand(ldist, udist);

        // Boundaries (ghost zones only)
        // (interior + boundaries = exterior)
        b.boundaries = b.exterior - intr;

      } // for ml
    } // for c
  } // for rl
}

// Loops over each refinement level, each component, and each multigrid
// level, executing the "boxesop" member function argument on the corresponding
// element of the "boxes" member 
template<int D>
void dh<D>::foreach_reflevel_component_mglevel (dh<D>::boxesop op) {

  for (int rl=0; rl<h.reflevels(); ++rl) {
    for (int c=0; c<h.components(rl); ++c) {
      for (int ml=0; ml<h.mglevels(rl,c); ++ml) {
        dboxes & box = boxes.at(rl).at(c).at(ml);
        (this->*op)( box, rl, c, ml ); // evaluate member function
      }
    }
  }
}

template<int D>
void dh<D>::setup_multigrid_boxes( dh<D>::dboxes & box, int rl, int c, int ml )
{
  const ibbox& intr = box.interior;
  const ibbox& extr = box.exterior;

  // Multigrid boxes
  if (ml>0) {
    dboxes & bbox = boxes.at(rl).at(c).at(ml-1);
    const ibbox intrf = bbox.interior;
    const ibbox extrf = bbox.exterior;
    // Restriction (interior)
    {
      // (the restriction must fill all of the interior of the
      // coarse grid, and may use the exterior of the fine grid)
      const ibbox recv = intr;
            assert (intr.empty() || ! recv.empty());
      const ibbox send = recv.expanded_for(extrf);
            assert (intr.empty() || ! send.empty());
      // TODO: put the check back in, taking outer boundaries
      // into account
#if 0
      assert (send.is_contained_in(extrf));
#endif
      bbox.send_mg_coarse.push_back(send);
      box .recv_mg_fine  .push_back(recv);
    }
    // Prolongation (interior)
    {
      // (the prolongation may use the exterior of the coarse
      // grid, and may fill only the interior of the fine grid,
      // and the bbox must be as large as possible)
      const ibbox recv = extr.contracted_for(intrf) & intrf;
            assert (intr.empty() || ! recv.empty());
      const ibbox send = recv.expanded_for(extr);
            assert (intr.empty() || ! send.empty());
      bbox.recv_mg_coarse.push_back(recv);
      box .send_mg_fine  .push_back(send);
    }
  } // if not finest multigrid level
}

template<int D>
void dh<D>::setup_sync_and_refine_boxes( dh<D>::dboxes & b, int rl, int c, int ml )
{
  // Sync boxes
  const int cs = h.components(rl);
  b.send_sync.resize(cs);
  b.recv_sync.resize(cs);

  // Refinement boxes
  if (rl>0) {
    const int csm1 = h.components(rl-1);
    b.send_ref_coarse.resize(csm1);
    b.recv_ref_coarse.resize(csm1);
    b.recv_ref_bnd_coarse.resize(csm1);
  }
  if (rl<h.reflevels()-1) {
    const int csp1 = h.components(rl+1);
    b.recv_ref_fine.resize(csp1);
    b.send_ref_fine.resize(csp1);
    b.send_ref_bnd_fine.resize(csp1);
  }
}

template<int D>
void dh<D>::intersect_sync_with_interior( dh<D>::dboxes & box, int rl, int c, int ml )
{
  const ibset& bnds = box.boundaries;

  // Sync boxes
  for (int cc=0; cc<h.components(rl); ++cc) {
    dboxes & box1 = boxes.at(rl).at(cc).at(ml);
    assert (ml<h.mglevels(rl,cc));
    // intersect boundaries with interior of that component
    ibset ovlp = bnds & box1.interior;
    ovlp.normalize();
    for (typename ibset::const_iterator b=ovlp.begin();b!=ovlp.end(); ++b) {
      box .recv_sync.at(cc).push_back(*b);
      box1.send_sync.at(c ).push_back(*b);
    }
  }
}

template<int D>
void dh<D>::setup_refinement_interior_boxes( dh<D>::dboxes & box, int rl, int c, int ml )
{
  const ibbox& intr = box.interior;
  const ibbox& extr = box.exterior;

  // Refinement boxes
  if (rl<h.reflevels()-1) {
    for (int cc=0; cc<h.components(rl+1); ++cc) {
      dboxes & box1 = boxes.at(rl+1).at(cc).at(ml);
      const ibbox intrf = box1.interior;
      // Prolongation (interior)
      // TODO: prefer boxes from the same processor
      {
        // (the prolongation may use the exterior of the coarse
        // grid, and must fill all of the interior of the fine
        // grid)
        const int pss = prolongation_stencil_size();
        ibset recvs = extr.expand(-pss,-pss).contracted_for(intrf) & intrf;
        const iblistvect& rrc = box1.recv_ref_coarse;
        for (typename iblistvect::const_iterator lvi=rrc.begin();
             lvi!=rrc.end(); ++lvi) {
          for (typename iblist::const_iterator li=lvi->begin();
               li!=lvi->end(); ++li) {
            recvs -= *li;
          }
        }
        recvs.normalize();
        assert (recvs.setsize() <= 1);
        if (recvs.setsize() == 1) {
          const ibbox recv = *recvs.begin();
          const ibbox send = recv.expanded_for(extr);
          assert (! send.empty());
          assert (send.is_contained_in(extr));
          box1.recv_ref_coarse.at(c ).push_back(recv);
          box. send_ref_fine  .at(cc).push_back(send);
        }
      }
    } // for cc
  } // if not finest refinement level
}

template<int D>
void dh<D>::setup_refinement_exterior_boxes( dh<D>::dboxes & box, int rl, int c, int ml )
{
  const ibbox& intr = box.interior;
  const ibbox& extr = box.exterior;

  // Refinement boxes
  if (rl<h.reflevels()-1) {
    for (int cc=0; cc<h.components(rl+1); ++cc) {
      dboxes & box1 = boxes.at(rl+1).at(cc).at(ml);
      const ibbox intrf = box1.interior;
            const ibbox& extrf = box1.exterior;
            const ibset& bndsf = box1.boundaries;
      // Prolongation (boundaries)
            // TODO: prefer boxes from the same processor
      {
        // (the prolongation may use the exterior of the coarse
        // grid, and must fill all of the boundary of the fine
        // grid)
        const int pss = prolongation_stencil_size();
        // Prolongation boundaries
        ibset pbndsf = bndsf;
        {
          // Do not count what is synced
          const iblistvect& rs = box1.recv_sync;
          for (typename iblistvect::const_iterator lvi=rs.begin();
               lvi!=rs.end(); ++lvi) {
            for (typename iblist::const_iterator li=lvi->begin();
                 li!=lvi->end(); ++li) {
              pbndsf -= *li;
            }
          }
          pbndsf.normalize();
        }
        // Buffer zones
        ibset buffers;
        {
          for (typename ibset::const_iterator pbi=pbndsf.begin();
               pbi!=pbndsf.end(); ++pbi) {
            buffers |= (*pbi).expand(buffer_width, buffer_width) & extrf;
          }
          buffers.normalize();
        }
        // Add boundaries
        const ibbox maxrecvs = extr.expand(-pss,-pss).contracted_for(extrf);
        ibset recvs = buffers & maxrecvs;
        recvs.normalize();
        {
          // Do not prolongate what is already prolongated
          const iblistvect& rrbc = box1.recv_ref_bnd_coarse;
          for (typename iblistvect::const_iterator lvi=rrbc.begin();
               lvi!=rrbc.end(); ++lvi) {
            for (typename iblist::const_iterator li=lvi->begin();
                 li!=lvi->end(); ++li) {
              recvs -= *li;
            }
          }
          recvs.normalize();
        }
        {
          for (typename ibset::const_iterator ri = recvs.begin();
               ri != recvs.end(); ++ri) {
            const ibbox & recv = *ri;
            const ibbox send = recv.expanded_for(extr);
            assert (! send.empty());
            assert (send.is_contained_in(extr));
            assert (send.is_contained_in(extr.expand(-pss,-pss)));
            box1.recv_ref_bnd_coarse.at(c ).push_back(recv);
            box .send_ref_bnd_fine  .at(cc).push_back(send);
          }
        }
      }
            
    } // for cc
  } // if not finest refinement level
}

template<int D>
void dh<D>::setup_restrict_interior_boxes( dh<D>::dboxes & box, int rl, int c, int ml )
{
  const ibbox& intr = box.interior;
  const ibbox& extr = box.exterior;

  // Refinement boxes
  if (rl<h.reflevels()-1) {
    for (int cc=0; cc<h.components(rl+1); ++cc) {
      dboxes & box1 = boxes.at(rl+1).at(cc).at(ml);
      const ibbox intrf = box1.interior;
      // Restriction (interior)
      {
        // (the restriction may fill the interior of the of the
        // coarse grid, and may use the interior of the fine
        // grid, and the bbox must be as large as possible)
        // (the restriction must not use points that are filled
        // by boundary prolongation)
        ibset sends = intrf & intr.expanded_for(intrf);
        // remove what is received during boundary prolongation
        for (int ccc=0; ccc<h.components(rl); ++ccc) {
          const iblist& sendlist = box1.recv_ref_bnd_coarse.at(ccc);
          for (typename iblist::const_iterator sli = sendlist.begin();
               sli != sendlist.end(); ++sli) {
            sends -= *sli;
          }
        }
        sends.normalize();
        for (typename ibset::const_iterator si = sends.begin();
             si != sends.end(); ++si) {
          const ibbox recv = (*si).contracted_for(intr);
          if (! recv.empty()) {
            const ibbox & send = recv.expanded_for(intrf);
            assert (! send.empty());
            box1.send_ref_coarse.at(c ).push_back(send);
            box .recv_ref_fine  .at(cc).push_back(recv);
          }
        }
      }
            
    } // for cc
  } // if not finest refinement level
}

template<int D>
void dh<D>::trim_unsynced_boundaries( dh<D>::dboxes & box, int rl, int c, int ml )
{
  // Boundaries that are not synced, or are neither synced nor
  // prolonged to from coarser grids (outer boundaries)
  ibset& sync_not = box.sync_not;
  ibset& recv_not = box.recv_not;
  
  // The whole boundary
  sync_not = box.boundaries;
  recv_not = box.boundaries;
  
  // Subtract boxes received during synchronisation
  const iblistvect& recv_sync = box.recv_sync;
  for (typename iblistvect::const_iterator lvi=recv_sync.begin();
       lvi!=recv_sync.end(); ++lvi) {
    for (typename iblist::const_iterator li=lvi->begin();
         li!=lvi->end(); ++li) {
      sync_not -= *li;
      recv_not -= *li;
    }
  }
  
  // Subtract boxes received during prolongation
  const iblistvect& recv_ref_bnd_coarse = box.recv_ref_bnd_coarse;
  for (typename iblistvect::const_iterator lvi=recv_ref_bnd_coarse.begin();
       lvi!=recv_ref_bnd_coarse.end(); ++lvi) {
    for (typename iblist::const_iterator li=lvi->begin();
         li!=lvi->end(); ++li) {
      recv_not -= *li;
    }
  }
}

template<int D>
void dh<D>::assert_assert_assert( dh<D>::dboxes & box, int rl, int c, int ml )
{
// Assert that all boundaries are synced or received
  {
    const ibset& sync_not = box.sync_not;
#if 0
    const ibset& recv_not = boxes.at(rl).at(c).at(ml).recv_not;
#endif
          
    // Check that no boundaries are left over
    if (rl==0) assert (sync_not.empty());
#if 0
    assert (recv_not.empty());
#endif
  }

  // Assert that the interior is received exactly once during
  // prolongation, and that nothing else is received
  {
    if (rl==0) {
      const iblistvect& recv_ref_coarse = box.recv_ref_coarse;
      assert (recv_ref_coarse.empty());
    } else {              // rl!=0
      const iblistvect& recv_ref_coarse = box.recv_ref_coarse;
      ibset intr = box.interior;
      for (typename iblistvect::const_iterator
             lvi=recv_ref_coarse.begin();
           lvi!=recv_ref_coarse.end(); ++lvi) {
        for (typename iblist::const_iterator li=lvi->begin();
             li!=lvi->end(); ++li) {
          const int old_sz = intr.size();
          const int this_sz = li->size();
          intr -= *li;
          const int new_sz = intr.size();
          // TODO
          assert (new_sz + this_sz == old_sz);
        }
      }
      // TODO
      // This need not be empty at outer boundaries.  Check that
      // those are indeed outer boundaries!  But what size of the
      // boundary region should be used for that?
#if 0
      assert (intr.empty());
#endif
    }
  }
  
  // Assert that the boundaries are received at most once during
  // prolongation and synchronisation, and that nothing else is
  // received
  {
    const iblistvect& recv_sync = box.recv_sync;
    const iblistvect& recv_ref_bnd_coarse = box.recv_ref_bnd_coarse;
    ibset bnds = box.boundaries;
    for (typename iblistvect::const_iterator lvi=recv_sync.begin();
         lvi!=recv_sync.end(); ++lvi) {
      for (typename iblist::const_iterator li=lvi->begin();
           li!=lvi->end(); ++li) {
        const int old_sz = bnds.size();
        const int this_sz = li->size();
        bnds -= *li;
        const int new_sz = bnds.size();
        assert (new_sz + this_sz == old_sz);
      }
    }
    for (typename iblistvect::const_iterator
           lvi=recv_ref_bnd_coarse.begin();
         lvi!=recv_ref_bnd_coarse.end(); ++lvi) {
      for (typename iblist::const_iterator li=lvi->begin();
           li!=lvi->end(); ++li) {
        const int old_sz = bnds.size();
        const int this_sz = li->size();
        bnds -= *li;
        const int new_sz = bnds.size();
        // TODO
        // The new size can be larger if part of the
        // prolongation went into the buffer zone.
//      assert (new_sz + this_sz == old_sz);
        assert (new_sz + this_sz >= old_sz);
      }
    }
    // TODO
    // This need not be empty at outer boundaries.  Check that
    // those are indeed outer boundaries!  But what size of the
    // boundary region should be used for that?
#if 0
    assert (bnds.empty());
#endif
  }
}

template<int D>
void dh<D>::calculate_bases () {
  // Calculate bases
  bases.resize(h.reflevels());
  for (int rl=0; rl<h.reflevels(); ++rl) {
    if (h.components(rl)==0) {
      bases.at(rl).resize(0);
    } else {
      bases.at(rl).resize(h.mglevels(rl,0));
      for (int ml=0; ml<h.mglevels(rl,0); ++ml) {
        dbases & base = bases.at(rl).at(ml);
        base.exterior = ibbox();
        base.interior = ibbox();
        for (int c=0; c<h.components(rl); ++c) {
          dboxes & box = boxes.at(rl).at(c).at(ml);
          base.exterior = (base.exterior.expanded_containing(box.exterior));
          base.interior = (base.interior.expanded_containing(box.interior));
        }
        base.boundaries = base.exterior - base.interior;
      }
    }
  }
}

template<int D>
void dh<D>::save_time ( bool do_prolongate ) {
  for (typename list<ggf<D>*>::iterator f=gfs.begin(); f!=gfs.end(); ++f) {
    (*f)->recompose_crop ();
  }
  for (int rl=0; rl<h.reflevels(); ++rl) {
    for (typename list<ggf<D>*>::iterator f=gfs.begin(); f!=gfs.end(); ++f) {
      (*f)->recompose_allocate (rl);
    }
    for (comm_state<D> state; !state.done(); state.step()) {
      for (typename list<ggf<D>*>::iterator f=gfs.begin(); f!=gfs.end(); ++f) {
        (*f)->recompose_fill (state, rl, do_prolongate);
      }
    }
    for (typename list<ggf<D>*>::reverse_iterator f=gfs.rbegin(); f!=gfs.rend(); ++f) {
      (*f)->recompose_free (rl);
    }
    for (comm_state<D> state; !state.done(); state.step()) {
      for (typename list<ggf<D>*>::iterator f=gfs.begin(); f!=gfs.end(); ++f) {
        (*f)->recompose_bnd_prolongate (state, rl, do_prolongate);
      }
    }
    for (comm_state<D> state; !state.done(); state.step()) {
      for (typename list<ggf<D>*>::iterator f=gfs.begin(); f!=gfs.end(); ++f) {
        (*f)->recompose_sync (state, rl, do_prolongate);
      }
    }
  } // for rl
}

template<int D>
void dh<D>::save_memory ( bool do_prolongate ) {
  ggf<D>* vectorleader = NULL;
  for (typename list<ggf<D>*>::iterator f=gfs.begin(); f!=gfs.end(); ++f) {
    
    (*f)->recompose_crop ();
    for (int rl=0; rl<h.reflevels(); ++rl) {
      (*f)->recompose_allocate (rl);
      for (comm_state<D> state; !state.done(); state.step()) {
        (*f)->recompose_fill (state, rl, do_prolongate);
      }
      assert ((*f)->vectorlength >= 1);
      if ((*f)->vectorlength == 1) {
        assert (! vectorleader);
        (*f)->recompose_free (rl);
      } else {
        // treat vector groups specially: delete the leader only
        // after all other elements have been deleted
        if ((*f)->vectorindex == 0) {
          // first element: delete nothing
          if (rl == 0) {
            assert (! vectorleader);
            vectorleader = *f;
          }
          assert (vectorleader);
        } else {
          assert (vectorleader);
          (*f)->recompose_free (rl);
          if ((*f)->vectorindex == (*f)->vectorlength-1) {
            // this was the last element: delete the leader as well
            vectorleader->recompose_free (rl);
            if (rl == h.reflevels()-1) {
              vectorleader = NULL;
            }
          }
        }
      }
      for (comm_state<D> state; !state.done(); state.step()) {
        (*f)->recompose_bnd_prolongate (state, rl, do_prolongate);
      }
      for (comm_state<D> state; !state.done(); state.step()) {
        (*f)->recompose_sync (state, rl, do_prolongate);
      }
    } // for rl
    
  } // for gf
  assert (! vectorleader);
}

// Grid function management
template<int D>
void dh<D>::add (ggf<D>* f) {
  CHECKPOINT;
  gfs.push_back(f);
}

template<int D>
void dh<D>::remove (ggf<D>* f) {
  CHECKPOINT;
  gfs.remove(f);
}



// Output
template<int D>
void dh<D>::output (ostream& os) const {
  os << "dh<" << D << ">:"
     << "ghosts=[" << lghosts << "," << ughosts << "],"
     << "gfs={";
  int cnt=0;
  for (typename list<ggf<D>*>::const_iterator f = gfs.begin();
       f != gfs.end(); ++f) {
    if (cnt++) os << ",";
    (*f)->output(os);
  }
  os << "}";
}

template<int D>
void dh<D>::output_bboxes( dh<D>::dboxes & box, int rl, int c, int ml )
{
  cout << endl;
  cout << "dh bboxes:" << endl;
  cout << "rl=" << rl << " c=" << c << " ml=" << ml << endl;
  cout << "exterior=" << box.exterior << endl;
  cout << "interior=" << box.interior << endl;
  cout << "send_mg_fine=" << box.send_mg_fine << endl;
  cout << "send_mg_coarse=" << box.send_mg_coarse << endl;
  cout << "recv_mg_fine=" << box.recv_mg_fine << endl;
  cout << "recv_mg_coarse=" << box.recv_mg_coarse << endl;
  cout << "send_ref_fine=" << box.send_ref_fine << endl;
  cout << "send_ref_coarse=" << box.send_ref_coarse << endl;
  cout << "recv_ref_fine=" << box.recv_ref_fine << endl;
  cout << "recv_ref_coarse=" << box.recv_ref_coarse << endl;
  cout << "send_sync=" << box.send_sync << endl;
  cout << "send_ref_bnd_fine=" << box.send_ref_bnd_fine << endl;
  cout << "boundaries=" << box.boundaries << endl;
  cout << "recv_sync=" << box.recv_sync << endl;
  cout << "recv_ref_bnd_coarse=" << box.recv_ref_bnd_coarse << endl;
  cout << "sync_not=" << box.sync_not << endl;
  cout << "recv_not=" << box.recv_not << endl;
}

template<int D>
void dh<D>::output_bases () {
  for (int rl=0; rl<h.reflevels(); ++rl) {
    if (h.components(rl)>0) {
      for (int ml=0; ml<h.mglevels(rl,0); ++ml) {
        dbases & base = bases.at(rl).at(ml);
        cout << endl;
        cout << "dh bases:" << endl;
        cout << "rl=" << rl << " ml=" << ml << endl;
        cout << "exterior=" << base.exterior << endl;
        cout << "interior=" << base.interior << endl;
        cout << "boundaries=" << base.boundaries << endl;
      }
    }
  }
}


template class dh<3>;
