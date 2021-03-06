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

#include "plugins/hdf5/DataBoxReader.hpp"
#include "TransformBox.hpp"
#include <math/complex/Complex.hpp>

namespace parataxis {
namespace plugins {
namespace hdf5 {

/** Reader that can read DataBoxes and can be specialized over the value type of the data box */
template<class T_ValueType>
struct DataBoxReader<PMacc::math::Complex<T_ValueType>>
{
    /// Read a field to a databox
    /// @param reader       Instance of SplashReader
    /// @param dataBox      Databox containing the data
    /// @param globalDomain Offset and Size of the field over all processes
    /// @param localSize    Size of the field on the current process (must match extents of data in the box)
    /// @param localOffset  Offset of the field of the current process
    template<class T_SplashReader, class T_DataBox, unsigned T_globalDims>
    void operator()(T_SplashReader& reader, const T_DataBox& dataBox,
            const PMacc::Selection<T_globalDims>& globalDomain,
            const PMacc::DataSpace<T_DataBox::Dim>& localSize,
            const PMacc::DataSpace<T_globalDims>& localOffset)
    {
        using ValueType = typename T_DataBox::ValueType;
        static_assert(std::is_same<PMacc::math::Complex<T_ValueType>, ValueType>::value, "Wrong type in dataBox");

        readDataBox(reader["real"], makeHostTransformBox(dataBox, [](ValueType& value) -> T_ValueType& { return value.get_real(); }), globalDomain, localSize, localOffset);
        readDataBox(reader["imag"], makeHostTransformBox(dataBox, [](ValueType& value) -> T_ValueType& { return value.get_imag(); }), globalDomain, localSize, localOffset);
    }
};

}  // namespace hdf5
}  // namespace plugins
}  // namespace parataxis
