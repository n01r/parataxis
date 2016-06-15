#pragma once

#include "xrtTypes.hpp"
#include <splash/splash.h>

namespace xrt {
namespace plugins {
namespace hdf5 {

/** Convert a PMacc::Selection instance into a splash::Domain */
template<unsigned T_dim>
splash::Domain makeSplashDomain(const PMacc::Selection<T_dim>& selection)
{
    splash::Domain splashDomain;

    for (uint32_t d = 0; d < T_dim; ++d)
    {
        splashDomain.getOffset()[d] = selection.offset[d];
        splashDomain.getSize()[d] = selection.size[d];
    }
    return splashDomain;
}

/** Convert offset and size as PMacc::DataSpace instances into a splash::Domain */
template<unsigned T_dim>
splash::Domain makeSplashDomain(const PMacc::DataSpace<T_dim>& offset, const PMacc::DataSpace<T_dim>& size)
{
    splash::Domain splashDomain;

    for (uint32_t d = 0; d < T_dim; ++d)
    {
        splashDomain.getOffset()[d] = offset[d];
        splashDomain.getSize()[d] = size[d];
    }
    return splashDomain;
}

template<unsigned T_dim>
splash::Dimensions makeSplashSize(const PMacc::DataSpace<T_dim>& size)
{
    splash::Dimensions splashSize;

    for (uint32_t d = 0; d < T_dim; ++d)
        splashSize[d] = size[d];
    return splashSize;
}

/** Check if a size contains numDims dimensions (excess=1) */
bool isSizeValid(const splash::Dimensions& size, unsigned numDims)
{
    assert(numDims > 0);
    for(unsigned d = numDims; d <= 3; d++)
    {
        if(size[d - 1] != 1)
            return false;
    }
    return true;
}

/** Check if an offset contains numDims dimensions (excess=0) */
bool isOffsetValid(const splash::Dimensions& offset, unsigned numDims)
{
    assert(numDims > 0);
    for(unsigned d = numDims; d <= 3; d++)
    {
        if(offset[d - 1] != 0)
            return false;
    }
    return true;
}

/** Check if a domain containing numDims dimensions is valid (excess offsets=0, sizes=1) */
bool isDomainValid(const splash::Domain& domain, unsigned numDims)
{
    return isSizeValid(domain.getSize(), numDims) &&
           isOffsetValid(domain.getOffset(), numDims);
}

}  // namespace hdf5
}  // namespace plugins
}  // namespace xrt
