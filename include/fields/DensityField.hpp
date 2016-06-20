#pragma once

#include "xrtTypes.hpp"

#include <dimensions/DataSpaceOperations.hpp>
#include <mappings/kernel/AreaMapping.hpp>
#include <eventSystem/EventSystem.hpp>
#include <dataManagement/ISimulationData.hpp>

#include <memory>

namespace xrt {
namespace fields {

    namespace kernel
    {
        template<class T_BoxWriteOnly, class T_Space, class T_Generator, class T_Mapping>
        __global__ void createDensityDistribution(T_BoxWriteOnly buffWrite, T_Space localDomainOffset, T_Generator generator, T_Mapping mapper)
        {
            /* get position in local domain in units of SuperCells for this block */
            const Space blockSuperCellIdx(mapper.getSuperCellIndex(Space(blockIdx)));
            /* convert position in unit of cells */
            const Space blockCellIdx = blockSuperCellIdx * T_Mapping::SuperCellSize::toRT();
            /* get offset to the blockCellIdx for this thread */
            const Space cellOffset(threadIdx);

            /* Calculate the global cellIdx by removing potential guard cells and adding the localDomainOffset*/
            const Space blockSuperCellOffset(mapper.getSuperCellIndex(Space()));
            const Space globalCellIdx = (blockSuperCellIdx - blockSuperCellOffset) * T_Mapping::SuperCellSize::toRT()
                                        + cellOffset + localDomainOffset;
            buffWrite(blockCellIdx + cellOffset) = generator(globalCellIdx);
        }
    }

    class DensityField: PMacc::ISimulationData
    {
    public:
        using Type = float_X;
        using Buffer = PMacc::GridBuffer< Type, simDim >;

    private:
        MappingDesc cellDescription;
        std::unique_ptr<Buffer> buffer;

    public:

        DensityField(const MappingDesc& desc): cellDescription(desc), buffer(new Buffer(cellDescription.getGridLayout()))
        {
            auto guardingCells(Space::create(1));
            for (uint32_t i = 1; i < PMacc::traits::NumberOfExchanges<simDim>::value; ++i)
            {
                buffer->addExchange(PMacc::GUARD, PMacc::Mask(i), guardingCells, static_cast<uint32_t>(CommTag::BUFF));
            }
        }

        static std::string
        getName()
        {
            return "DensityField";
        }

        PMacc::SimulationDataId getUniqueId() override
        {
            return getName();
        }

        void synchronize() override
        {
            buffer->deviceToHost();
        }

        void init()
        {
            Environment::get().DataConnector().registerData(*this);
        }

        void reset()
        {
            buffer->reset(false);
        }

        template<class T_Generator>
        void createDensityDistribution(T_Generator&& generator)
        {
            __cudaKernelArea(kernel::createDensityDistribution, cellDescription, PMacc::CORE + PMacc::BORDER)
                    (MappingDesc::SuperCellSize::toRT().toDim3())
                    (buffer->getDeviceBuffer().getDataBox(),
                     Environment::get().SubGrid().getLocalDomain().offset,
                     generator);
        }

        typename Buffer::DataBoxType
        getHostDataBox()
        {
            return buffer->getHostBuffer().getDataBox();
        }

        typename Buffer::DataBoxType
        getDeviceDataBox()
        {
            return buffer->getDeviceBuffer().getDataBox();
        }

        Buffer&
        getGridBuffer()
        {
            return *buffer;
        }

        static float_64
        getUnit()
        {
            return 1;
        }

        static std::vector<float_64>
        getUnitDimension()
        {
            return std::vector<float_64>(7, 0);
        }

    };


}  // namespace fields
}  // namespace xrt