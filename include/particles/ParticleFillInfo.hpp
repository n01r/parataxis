/**
 * Copyright 2015-2016 Alexander Grund
 *
 * This file is part of ParaTAXIS.
 *
 * ParaTAXIS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ParaTAXIS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ParaTAXIS.  If not, see <http://www.gnu.org/licenses/>.
 */
 
#pragma once

#include "parataxisTypes.hpp"
#include "traits/stdRenamings.hpp"

namespace parataxis {
namespace particles {

    /**
     * Collection of policies related to filling the grid with particles
     *
     * \tparam T_PhotCount Returns the number of photons for a given cell and time
     *                     CTor(uint32_t timeStep)
     *                     init(Space2D localCellIdx)
     *                     Functor: float_X()
     * \tparam T_Count     Returns the number of particles for a given cell and time
     *                     CTor(uint32_t timeStep)
     *                     init(Space2D localCellIdx)
     *                     Functor: uint32_t(float_X numPhotons)
     * \tparam T_Position  Returns the in-cell position for a given cell and particle number
     *                     CTor(uint32_t timeStep)
     *                     init(Space2D localCellIdx)
     *                     setCount(uint32_t particleCount)
     *                     Functor: float_D(uint32_t numParticle), gets called for all particles
     *                      with i in [0, particleCount)
     * \tparam T_Phase     Returns the phase of the particles in a given cell at a given time
     *                     CTor(uint32_t timeStep, float_64 phi_0)
     *                     init(Space2D localCellIdx)
     *                     Functor: float_X()
     * \tparam T_Direction Returns the initial direction of the particles for a given cell and time
     *                     CTor(uint32_t timeStep)
     *                     init(Space2D localCellIdx)
     *                     Functor: floatD_X()
     *
     */
    template<
        class T_PhotCount,
        class T_Count,
        class T_Position,
        class T_Phase,
        class T_Direction
    >
    struct ParticleFillInfo
    {
        T_PhotCount getPhotonCount;
    private:
        T_Count getCount_;
    public:
        T_Position getPosition;
        T_Phase getPhase;
        T_Direction getDirection;

        ParticleFillInfo(uint32_t timestep, float_64 phi_0):
            getPhotonCount(timestep), getCount_(timestep), getPosition(timestep), getPhase(timestep, phi_0), getDirection(timestep)
        {
            static_assert(std::is_same<traits::result_of_t<T_PhotCount()>, float_X>::value, "PhotCount must return a float_X");
            static_assert(std::is_same<traits::result_of_t<T_Count(float_X)>, uint32_t>::value, "Count must return an uint32_t");
        }

        ParticleFillInfo(const T_PhotCount& photCount, const T_Count& count, const T_Position& position, const T_Phase& phase, const T_Direction& direction):
            getPhotonCount(photCount), getCount_(count), getPosition(position), getPhase(phase), getDirection(direction)
        {}

        HDINLINE void
        init(Space localCellIdx)
        {
            getPhotonCount.init(localCellIdx);
            getCount_.init(localCellIdx);
            getPosition.init(localCellIdx);
            getPhase.init(localCellIdx);
            getDirection.init(localCellIdx);
        }

        HDINLINE uint32_t
        getCount(float_X numPhotons)
        {
            const uint32_t ct = getCount_(numPhotons);
            getPosition.setCount(ct);
            return ct;
        }
    };

    template<
            class T_PhotCount,
            class T_Count,
            class T_Position,
            class T_Phase,
            class T_Direction
        >
    ParticleFillInfo<T_PhotCount, T_Count, T_Position, T_Phase, T_Direction>
    getParticleFillInfo(const T_PhotCount& photCount, const T_Count& count, const T_Position& position, const T_Phase& phase, const T_Direction& direction)
    {
        return ParticleFillInfo<T_PhotCount, T_Count, T_Position, T_Phase, T_Direction>(photCount, count, position, phase, direction);
    }

}  // namespace particles
}  // namespace parataxis
