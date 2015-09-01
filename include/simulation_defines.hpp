#pragma once

namespace xrt {

    namespace bmpl = boost::mpl;
    enum class CommTag: uint32_t
    {
        NO_COMMUNICATION,
        BUFF,
        SPECIES_FIRSTTAG /* This needs to be the last one! */
    };

}  // namespace xrt

/* Use #include <> to allow user overrides */
#include <simulation_defines/_defaultParam.loader>
#include <simulation_defines/extensionParam.loader>

#include <simulation_defines/_defaultUnitless.loader>