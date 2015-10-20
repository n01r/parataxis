#pragma once

#include "xrtTypes.hpp"
#include "Random.hpp"
#include <algorithms/math.hpp>

namespace xrt {
namespace particles {
namespace scatterer {

    /**
     * Scatterer that changes the momentum based on 2 random angles (spherical coordinates)
     */
    template<class T_Config, class T_Species = bmpl::_1>
    struct RandomDirection
    {
        using Config = T_Config;

        HINLINE explicit
        RandomDirection(uint32_t currentStep)
        {}

        DINLINE void
        init(Space totalCellIdx)
        {
            rand.init(totalCellIdx);
        }

        template<class T_DensityBox, typename T_Position, typename T_Momentum>
        DINLINE void
        operator()(const T_DensityBox& density, const T_Position& pos, T_Momentum& mom)
        {
            float_X polarAngle   = rand() * float_X(Config::maxPolar - Config::minPolar) + float_X(Config::minPolar);
            float_X azimuthAngle = acos(rand() * cos(float_X(Config::maxAzimuth - Config::minAzimuth)) + cos(float_X(Config::minAzimuth)));
            float_X sinPolar, cosPolar, sinAzimuth, cosAzimuth;
            PMaccMath::sincos(polarAngle, sinPolar, cosPolar);
            PMaccMath::sincos(azimuthAngle, sinAzimuth, cosAzimuth);
            mom.x() = sinPolar * cosAzimuth;
            mom.y() = sinPolar * sinAzimuth;
            mom.z() = cosPolar;
        }

    private:
        PMACC_ALIGN8(rand, Random<>);
    };

}  // namespace scatterer
}  // namespace particles
}  // namespace xrt
