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
#include "particles/functors/IterateSpecies.hpp"
#include "particles/functors/CopySpeciesToHost.hpp"
#include "particles/filters/IndexFilter.hpp"
#include "plugins/ISimulationPlugin.hpp"
#include "debug/LogLevels.hpp"

#include <particles/memory/buffers/MallocMCBuffer.hpp>
#include <mappings/kernel/AreaMapping.hpp>
#include <dataManagement/DataConnector.hpp>
#include <traits/HasIdentifier.hpp>
#include <debug/VerboseLog.hpp>
#include <string>

namespace parataxis {
namespace plugins {

    namespace detail {

        template<class T_Particle, bool T_hasId = PMacc::traits::HasIdentifier<T_Particle, particleId>::type::value>
        struct GetId
        {
            static uint64_t get(const T_Particle& particle)
            {
                return particle[particleId_];
            }
        };

        template<class T_Particle>
        struct GetId<T_Particle, false>
        {
            static uint64_t get(const T_Particle& particle)
            {
                return 0;
            }
        };

        template<class T_ParticlesType>
        struct PrintParticle
        {
            template<class T_Particle>
            void
            operator()(const Space globalIdx, const T_Particle& particle)
            {
                // Convert global + local position to position in µm
                floatD_64 pos;
                for(uint32_t i=0; i<simDim; ++i)
                    pos[i] = (float_64(globalIdx[i]) + particle[position_][i]) * cellSize[i] * UNIT_LENGTH * 1e6;

                uint64_t id = GetId<T_Particle>::get(particle);
                std::cout << "Particle " << globalIdx << " (" << T_ParticlesType::FrameType::getName() << id << "): " << " => " << pos << "[µm]\n";
            }
        };

    }  // namespace detail

    template<class T_ParticlesType>
    class PrintParticles : public ISimulationPlugin
    {
        using ParticlesType = T_ParticlesType;

        typedef MappingDesc::SuperCellSize SuperCellSize;
        typedef floatD_X FloatPos;

        uint32_t notifyFrequency;

        std::string analyzerName;
        std::string analyzerPrefix;
        std::vector<uint32_t> idxOffset;
        std::vector<uint32_t> idxSize;

        Space idxOff, idxSz;

    public:
        PrintParticles():
            notifyFrequency(0),
            analyzerName("PositionsParticles: write position of all particles of a species to std::cout"),
            analyzerPrefix(ParticlesType::FrameType::getName() + std::string("_position"))
        {
            Environment::get().PluginConnector().registerPlugin(this);
        }

        virtual ~PrintParticles()
        {}

        void pluginRegisterHelp(po::options_description& desc) override
        {
            desc.add_options()
                ((analyzerPrefix + ".period").c_str(), po::value<uint32_t >(&notifyFrequency), "enable analyzer [for each n-th step]")
                ((analyzerPrefix + ".offset").c_str(), po::value<std::vector<uint32_t> >(&idxOffset)->multitoken(), "Print only particles of cells with idx greater than this")
                ((analyzerPrefix + ".size").c_str(), po::value<std::vector<uint32_t> >(&idxSize)->multitoken(), "Print only particles of that many cells (in each dimension); 0 = all")
                ;
        }

        std::string pluginGetName() const override
        {
            return analyzerName;
        }

        void notify(uint32_t currentStep) override
        {
            PMacc::log< PARATAXISLogLvl::IN_OUT >("Printing particles at timestep %1% (%2%ns)") % currentStep % (currentStep * DELTA_T * UNIT_TIME * 1e9);
            PMacc::DataConnector &dc = Environment::get().DataConnector();

            /* synchronizes the MallocMCBuffer to the host side */
            PMacc::MallocMCBuffer& mallocMCBuffer = dc.getData<PMacc::MallocMCBuffer>(PMacc::MallocMCBuffer::getName());

            uint32_t particlesCount = 0;
            auto& particles = dc.getData<PIC_Photons>(PIC_Photons::FrameType::getName());
            const Space localOffset = Environment::get().SubGrid().getLocalDomain().offset;
            PMacc::AreaMapping< PMacc::CORE + PMacc::BORDER, MappingDesc > mapper(*cellDescription_);
            particles::functors::IterateSpecies<PIC_Photons>()(
                    particlesCount,
                    particles.getHostParticlesBox(mallocMCBuffer.getOffset()),
                    localOffset,
                    mapper,
                    particles::filters::IndexFilter(idxOff, idxSz),
                    detail::PrintParticle<ParticlesType>()
                    );

            dc.releaseData(PIC_Photons::FrameType::getName());
            dc.releaseData(PMacc::MallocMCBuffer::getName());
            PMacc::log< PARATAXISLogLvl::IN_OUT >("%1% particles printed") % particlesCount;
        }

        void checkpoint(uint32_t currentStep, const std::string checkpointDirectory) override
        {}
        void restart(uint32_t restartStep, const std::string restartDirectory) override
        {}

    protected:
        void pluginLoad() override
        {
            if(!notifyFrequency)
                return;

            Environment::get().PluginConnector().setNotificationPeriod(this, notifyFrequency);
            auto& subGrid = Environment::get().SubGrid();
            Space totalSize = subGrid.getTotalDomain().size;
            if(idxOffset.empty() && idxSize.empty())
            {
                // Use an area from the center as the default
                Space offset = subGrid.getTotalDomain().offset + totalSize / 2;
                for(uint32_t i = 0; i<simDim; ++i)
                {
                    idxOffset.push_back(offset[i]);
                    idxSize.push_back(5);
                }
                idxOffset[0] = subGrid.getTotalDomain().offset[0];
                idxSize[0] = totalSize[0];
            }
            idxOffset.resize(simDim);
            idxSize.resize(simDim, 0);
            for(uint32_t i = 0; i<simDim; ++i)
            {
                idxOff[i] = (idxOffset[i] < totalSize[i]) ? idxOffset[i] : 0;
                idxSz[i]  = (idxSize[i] > 0) ? idxSize[i] : totalSize[i];
            }
            PMacc::log< PARATAXISLogLvl::PLUGINS >("Printing particles in range %1% [%2%] every %3% timesteps") % idxOff % idxSz % notifyFrequency;
        }
    };

}  // namespace plugins
}  // namespace parataxis
