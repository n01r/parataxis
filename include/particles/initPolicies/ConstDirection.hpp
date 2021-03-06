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
#include "ToVector.hpp"

namespace parataxis {
namespace particles {
namespace initPolicies {

    /**
     * Same direction for all particles
     */
    template<class T_Config>
    struct ConstDirection
    {
        using Config = T_Config;

        ConstDirection(uint32_t  /*timestep*/){}

        HDINLINE void
        init(Space /*localCellIdx*/)
        {}

        DINLINE direction::type
        operator()()
        {
            const direction::type dir =  ToVector<Config, direction::type::dim>()();
            // We need unit vectors!
            return dir / PMaccMath::sqrt<sqrt_X>(PMaccMath::abs2(dir));
        }
    };

}  // namespace initPolicies
}  // namespace particles
}  // namespace parataxis
