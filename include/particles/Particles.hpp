#pragma once

#include "xrtTypes.hpp"
#include <particles/ParticlesBase.hpp>
#include <particles/memory/buffers/ParticlesBuffer.hpp>

#include <dataManagement/ISimulationData.hpp>

namespace xrt{

    class DensityField;

    template<typename T_ParticleDescription>
    class Particles : public PMacc::ParticlesBase<T_ParticleDescription, MappingDesc>, public PMacc::ISimulationData
    {
    public:

        typedef PMacc::ParticlesBase<T_ParticleDescription, MappingDesc> ParticlesBaseType;
        typedef typename ParticlesBaseType::BufferType BufferType;
        typedef typename ParticlesBaseType::FrameType FrameType;
        typedef typename ParticlesBaseType::FrameTypeBorder FrameTypeBorder;
        typedef typename ParticlesBaseType::ParticlesBoxType ParticlesBoxType;

        Particles(MappingDesc cellDescription, PMacc::SimulationDataId datasetID);

        virtual ~Particles();

        void createParticleBuffer();

        void init(DensityField* densityField);
        /**
         * Adds particles to the grid
         * \tparam T_DistributionFunctor Functor that returns number of particles for a given total GPU cell idx
         * \tparam T_PositionFunctor     Functor that returns a position for a given particle idx
         *                               Must also provide an init(totalGPUCellIdx, totalNumParToCreate) function
         */
        template<typename T_InitFunctor>
        void add(T_InitFunctor&& initFunctor, uint32_t timeStep);

        void update(uint32_t currentStep);

        PMacc::SimulationDataId getUniqueId() override;

        /** sync device data to host
         *
         * ATTENTION: - in the current implementation only supercell meta data are copied!
         *            - the shared (between all species) mallocMC buffer must be copied once
         *              by the user
         */
        void synchronize() override;

        void syncToDevice() override;

        /**
         * Handles particles that went out of the volume
         * @param direction
         */
        void processLeavingParticles(int32_t direction);

    private:
        PMacc::SimulationDataId datasetID;
        PMacc::GridLayout<simDim> gridLayout;
        DensityField* densityField_;
        uint32_t lastProcessedStep_;
    };

} //namespace xrt
