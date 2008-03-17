#include <cassert>
#include <cstdlib>
#include <sstream>
#include <string>

#include "cctk.h"
#include "cctk_Parameters.h"

#include "carpet.hh"

#include "data_region.hh"



namespace CarpetIOF5 {
  
  namespace F5 {
    
    using std::ostringstream;
    
    
    
    data_region_t::
    data_region_t (tensor_component_t & tensor_component,
                   bbox<int, dim> const & region)
      : m_tensor_component (tensor_component),
        m_region (region)
    {
      DECLARE_CCTK_PARAMETERS;
      
      assert (not region.empty());
      
      ostringstream namebuf;
#if 0
      namebuf << "map=" << Carpet::map << " "
              << "region=" << m_region;
#else
      namebuf << "region=" << m_region;
#endif
      string const namestr = namebuf.str();
      char const * const name = namestr.c_str();
      assert (name != 0);
      
      int const vartype = CCTK_VarTypeI (m_tensor_component.get_variable());
      assert (vartype >= 0);
      hid_t const hdf5_datatype = hdf5_datatype_from_cactus_datatype (vartype);
      assert (hdf5_datatype >= 0);
      
      vect<hsize_t, dim> const dims
        = (region.shape() / region.stride()).reverse();
      m_dataspace = H5Screate_simple (dim, & dims [0], 0);
      assert (m_dataspace >= 0);
      
      m_properties = H5Pcreate (H5P_DATASET_CREATE);
      assert (m_properties >= 0);
      if (compression_level > 0)
      {
        herr_t const herr1 = H5Pset_chunk (m_properties, dim, & dims [0]);
        assert (not herr1);
        herr_t const herr2 = H5Pset_deflate (m_properties, compression_level);
        assert (not herr2);
      }
      
      m_dataset
        = H5Dcreate (m_tensor_component.get_hdf5_tensor_component(), name,
                     hdf5_datatype, m_dataspace,
                     H5P_DEFAULT, m_properties, H5P_DEFAULT);
      assert (m_dataset >= 0);
      
      write_or_check_attribute
        (m_dataset, "iorigin", region.lower() / region.stride());
      
      assert (invariant());
    }
    
    
    
    data_region_t::
    ~ data_region_t ()
    {
      herr_t herr;
      
      herr = H5Dclose (m_dataset);
      assert (not herr);
      
      herr = H5Sclose (m_dataspace);
      assert (not herr);
      
      herr = H5Pclose (m_properties);
      assert (not herr);
    }
    
    
    
    string data_region_t::
    name_from_region (bbox<int, dim> const & region)
    {
      ostringstream namebuf;
#if 0
      namebuf << "map=" << Carpet::map << " "
              << "region=" << region;
#else
      namebuf << "region=" << region;
#endif
      return namebuf.str();
    }
    
    
    
    tensor_component_t & data_region_t::
    get_tensor_component ()
      const
    {
      return m_tensor_component;
    }
    
    
    
    void data_region_t::
    write (void const * const data,
           int const cactus_datatype)
      const
    {
      hid_t const memory_hdf5_datatype
        = hdf5_datatype_from_cactus_datatype (cactus_datatype);
      assert (memory_hdf5_datatype >= 0);
      
      vect<hsize_t, dim> const dims
        = (m_region.shape() / m_region.stride()).reverse();
      hid_t const memory_dataspace
        = H5Screate_simple (dim, & dims [0], & dims [0]);
      assert (memory_dataspace >= 0);
      
      hid_t const transfer_properties = H5Pcreate (H5P_DATASET_XFER);
      assert (transfer_properties >= 0);
      
      herr_t herr;
      herr
        = H5Dwrite (m_dataset, memory_hdf5_datatype, memory_dataspace,
                    m_dataspace, transfer_properties, data);
      assert (not herr);
      
      herr = H5Pclose (transfer_properties);
      assert (not herr);
      
      herr = H5Sclose (memory_dataspace);
      assert (not herr);
    }
    
    
    
    bool data_region_t::
    invariant ()
      const
    {
      return (not m_region.empty()
              and m_properties >= 0
              and m_dataset >= 0
              and m_dataspace >= 0);
    }
    
  } // namespace F5

} // namespace CarpetIOF5
