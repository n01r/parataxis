/**
 * Copyright 2013-2016 Axel Huebl, Rene Widera, Alexander Grund
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

#include "convertToSpace.hpp"
#include "particles/Particles.tpp"
#include "particles/ParticleFillInfo.hpp"
#include "LaserSource.hpp"

#include "fields/DensityField.hpp"
#include "fields/IFieldManipulator.hpp"
#include "generators.hpp"
#include "TimerIntervallExt.hpp"
#include "debug/LogLevels.hpp"

#include <particles/memory/buffers/MallocMCBuffer.hpp>
#include <particles/IdProvider.hpp>
#include <simulationControl/SimulationHelper.hpp>
#include <dimensions/DataSpace.hpp>
#include <mappings/kernel/MappingDescription.hpp>
#include <traits/NumberOfExchanges.hpp>
#include <compileTime/conversion/TypeToPointerPair.hpp>
#include <debug/VerboseLog.hpp>
#include <eventSystem/EventSystem.hpp>
#include <communication/AsyncCommunication.hpp>

#include <mpi/SeedPerRank.hpp>
#include <boost/program_options.hpp>
#include <cuda_profiler_api.h>
#include <memory>
#include <vector>

namespace parataxis {

    namespace po   = boost::program_options;

    class Simulation: public PMacc::SimulationHelper<simDim>
    {
        static_assert(simDim == 2 || simDim == 3, "Only 2D or 3D sims are allowed");
        using Parent = PMacc::SimulationHelper<simDim>;
        using Detector = Resolve_t<detector::PhotonDetector>;

        PMacc::MallocMCBuffer *mallocMCBuffer;

        PIC_Photons* particleStorage;
        LaserSource<PIC_Photons> laserSource;

        std::vector<uint32_t> gridSize, devices, detectorSize;
        uint32_t globalSeed;

        /* Only valid after pluginLoad */
        MappingDesc cellDescription;

        std::unique_ptr<fields::DensityField> densityField;
        std::unique_ptr<Detector> detector_;
#if !PARATAXIS_USE_SLOW_RNG
        std::unique_ptr<RNGProvider> rngProvider_;
#endif
        // Created with the Simulation class
        std::unique_ptr<fields::IFieldManipulator> fieldManipulator_;

    public:

        Simulation() :
            mallocMCBuffer(nullptr),
            particleStorage(nullptr),
            globalSeed(42),
            fieldManipulator_(new Resolve_t<FieldManipulator>)
        {}

        virtual ~Simulation()
        {
            mallocMC::finalizeHeap();
        }

        void notify(uint32_t currentStep) override
        {}

        void pluginRegisterHelp(po::options_description& desc) override
        {
            SimulationHelper<simDim>::pluginRegisterHelp(desc);
            desc.add_options()
                ("devices,d", po::value<std::vector<uint32_t> > (&devices)->multitoken(), "number of devices in each dimension")
                ("grid,g", po::value<std::vector<uint32_t> > (&gridSize)->multitoken(), "size of the simulation grid")
                ("detSize", po::value<std::vector<uint32_t> > (&detectorSize)->multitoken(), "size of detector")
                ("globalSeed", po::value<uint32_t>(&globalSeed)->default_value(42), "Global seed used for RNGs")
                ;
        }

        std::string pluginGetName() const override
        {
            return "X-Ray Tracing";
        }

        void init() override
        {
#ifndef NDEBUG
            CUDA_CHECK(cudaDeviceSetLimit(cudaLimitPrintfFifoSize, 3 * MiB));
#endif
            TimeIntervallExt timer;
            PMacc::log<PARATAXISLogLvl::SIM_STATE>("Creating buffers");
            densityField.reset(new fields::DensityField(cellDescription));
            detector_.reset(new Detector(Space2D(detectorSize[0], detectorSize[1])));
#if !PARATAXIS_USE_SLOW_RNG
            rngProvider_.reset(new RNGProvider(Environment::get().SubGrid().getLocalDomain().size));
#endif
            PMacc::log(PARATAXISLogLvl::SIM_STATE() + PARATAXISLogLvl::TIMING(), "Done in %1%") % timer.printCurIntervallRestart();

            PMacc::log<PARATAXISLogLvl::SIM_STATE>("Creating particle buffers");;
            /* create particle storage (does NOT use mallocMC) */
            particleStorage = new PIC_Photons(cellDescription, PIC_Photons::FrameType::getName());
            PMacc::log(PARATAXISLogLvl::SIM_STATE() + PARATAXISLogLvl::TIMING(), "Done in %1%") % timer.printCurIntervallRestart();

            PMacc::log<PARATAXISLogLvl::SIM_STATE>("Initializing MallocMC");
            /* After all memory consuming stuff is initialized we can setup mallocMC with the remaining memory */
            initMallocMC();
            PMacc::log(PARATAXISLogLvl::SIM_STATE() + PARATAXISLogLvl::TIMING(), "Done in %1%") % timer.printCurIntervallRestart();

            particleStorage->createParticleBuffer();

            size_t freeGpuMem(0);
            Environment::get().MemoryInfo().getMemoryInfo(&freeGpuMem);
            PMacc::log< PARATAXISLogLvl::MEMORY > ("free mem after all mem is allocated %1% MiB") % (freeGpuMem / MiB);

            PMacc::log<PARATAXISLogLvl::SIM_STATE>("Initializing density field");
            densityField->init();
            PMacc::log(PARATAXISLogLvl::SIM_STATE() + PARATAXISLogLvl::TIMING(), "Done in %1%") % timer.printCurIntervallRestart();
            PMacc::log<PARATAXISLogLvl::SIM_STATE>("Initializing detector");
            detector_->init();
            PMacc::log(PARATAXISLogLvl::SIM_STATE() + PARATAXISLogLvl::TIMING(), "Done in %1%") % timer.printCurIntervallRestart();
            PMacc::log<PARATAXISLogLvl::SIM_STATE>("Initializing particles");
            particleStorage->init(densityField.get());
            PMacc::log(PARATAXISLogLvl::SIM_STATE() + PARATAXISLogLvl::TIMING(), "Done in %1%") % timer.printCurIntervallRestart();
            PMacc::log<PARATAXISLogLvl::SIM_STATE>("Initializing laser source");
            laserSource.init();
            PMacc::log(PARATAXISLogLvl::SIM_STATE() + PARATAXISLogLvl::TIMING(), "Done in %1%") % timer.printCurIntervallRestart();

            PMacc::log< PARATAXISLogLvl::SIM_STATE > ("Simulation initialized.");
        }

        uint32_t fillSimulation() override
        {
            uint32_t step = 0;
            if (this->restartRequested)
            {
                std::vector<uint32_t> checkpoints = readCheckpointMasterFile();
                if (checkpoints.empty())
                     throw std::runtime_error("Restart failed, no checkpoints found.");
                if (restartStep < 0)
                    this->restartStep = checkpoints.back();
                else if(std::find(checkpoints.begin(), checkpoints.end(), restartStep) == checkpoints.end())
                    throw std::runtime_error("Restart failed, invalid restartStep passed.");

                // restart simulation by loading from persistent data
                // the simulation will start after restartStep
                PMacc::log<PARATAXISLogLvl::SIM_STATE>("Restarting simulation from timestep %1% in directory '%2%'") %
                    restartStep % restartDirectory;

                Environment::get().PluginConnector().restartPlugins(restartStep, restartDirectory);
                __getTransactionEvent().waitForFinished();

                // Global synchronize
                CUDA_CHECK(cudaDeviceSynchronize());
                CUDA_CHECK(cudaGetLastError());
                auto&gc = Environment::get().GridController();
                MPI_CHECK(MPI_Barrier(gc.getCommunicator().getMPIComm()));

                PMacc::log<PARATAXISLogLvl::SIM_STATE>("Loading from persistent data finished");

                step = this->restartStep + 1;
            } else
            {
                PMacc::log<PARATAXISLogLvl::SIM_STATE>("Starting simulation from timestep 0");

                PMacc::IdProvider<simDim>::init();
                TimeIntervallExt timer;

#if !PARATAXIS_USE_SLOW_RNG
                PMacc::log<PARATAXISLogLvl::SIM_STATE>("Initializing random number generators");
                PMacc::mpi::SeedPerRank<simDim> seedPerRank;
                uint32_t seed = seeds::xorRNG ^ seeds::Global()();
                seed = seedPerRank(seed);
                rngProvider_->init(seed);
                PMacc::log(PARATAXISLogLvl::SIM_STATE() + PARATAXISLogLvl::TIMING(), "Done in %1%") % timer.printCurIntervallRestart();
#endif

                PMacc::log<PARATAXISLogLvl::SIM_STATE>("Creating density distribution");
                Resolve_t<initialDensity::Generator> generator;
                densityField->createDensityDistribution(generator);
                PMacc::log(PARATAXISLogLvl::SIM_STATE() + PARATAXISLogLvl::TIMING(), "Done in %1%") % timer.printCurIntervallRestart();
            }

            PMacc::log< PARATAXISLogLvl::SIM_STATE > ("Simulation filled.");

#if PARATAXIS_CHECK_PHOTON_CT
            laserSource.checkPhotonCt(runSteps, this->cellDescription);
#endif
            return step;
        }

        void resetAll(uint32_t currentStep)
        {
            if(currentStep > 0)
                std::cerr << "Cannot reset to a timestep. Resetting to zero" << std::endl;
            particleStorage->reset(currentStep);
            laserSource.reset(currentStep);
            densityField->reset();
            detector_->reset();
        }

        void movingWindowCheck(uint32_t currentStep) override
        {}

        /**
         * Run one simulation step.
         *
         * @param currentStep iteration number of the current step
         */
        void runOneStep(uint32_t currentStep) override
        {
#if (PARATAXIS_NVPROF_NUM_TS>0)
            if(currentStep == PARATAXIS_NVPROF_START_TS)
            {
                CUDA_CHECK(cudaDeviceSynchronize());
                CUDA_CHECK(cudaProfilerStart());
            }else if(currentStep == PARATAXIS_NVPROF_START_TS + PARATAXIS_NVPROF_NUM_TS)
            {
                CUDA_CHECK(cudaDeviceSynchronize());
                CUDA_CHECK(cudaProfilerStop());
            }
#endif
            if(currentStep % 128)
                CUDA_CHECK(cudaPeekAtLastError());
            fieldManipulator_->update(currentStep);
            laserSource.update(currentStep);
            particleStorage->update(currentStep);
            PMacc::EventTask commEvt = PMacc::communication::asyncCommunication(*particleStorage, __getTransactionEvent());
            __setTransactionEvent(commEvt);
        }

        const MappingDesc*
        getMappingDesc()
        {
            return &cellDescription;
        }

    protected:
        void pluginLoad() override
        {
            if(seeds::Global::value != globalSeed)
            {
                seeds::Global::value = globalSeed;
                PMacc::log<PARATAXISLogLvl::DOMAINS>("Using custom seed %1%") % globalSeed;
            }

            Space periodic = Space::create(0); // Non periodic boundaries!
            Space devices   = convertToSpace(this->devices, 1, "devices (-d)");
            Space gridSize  = convertToSpace(this->gridSize, 1, "grid (-g)");
            while(detectorSize.size() < 2)
                detectorSize.push_back(1024);

            /* Set up device mappings and create streams etc. */
            Environment::get().initDevices(devices, periodic);

            /* Divide grid evenly among devices */
            auto& gc = Environment::get().GridController();
            Space localGridSize(gridSize / devices);
            Space localGridOffset(gc.getPosition() * localGridSize);
            /* Set up environment (subGrid and singletons) with this size */
            Environment::get().initGrids( gridSize, localGridSize, localGridOffset);
            PMacc::log< PARATAXISLogLvl::DOMAINS > ("rank %1%; local size %2%; local offset %3%;") %
                    gc.getPosition().toString() % localGridSize.toString() % localGridOffset.toString();

            Parent::pluginLoad();

            /* Our layout is the subGrid with guard cells as big as one super cell */
            GridLayout layout(localGridSize, MappingDesc::SuperCellSize::toRT());
            /* Create with 1 border and 1 guard super cell */
            cellDescription = MappingDesc(layout.getDataSpace(), 1, 1);
            checkGridConfiguration(gridSize, layout);
        }

        void pluginUnload() override
        {
            Parent::pluginUnload();

            __delete(mallocMCBuffer);
            __delete(particleStorage);
        }

        void initMallocMC()
        {
            size_t freeGpuMem(0);
            Environment::get().MemoryInfo().getMemoryInfo(&freeGpuMem);
            if(freeGpuMem < reservedGPUMemorySize)
            {
                PMacc::log< PARATAXISLogLvl::MEMORY > ("%1% MiB free memory < %2% MiB required reserved memory")
                    % (freeGpuMem / MiB) % (reservedGPUMemorySize / MiB) ;
                throw std::runtime_error("Cannot reserve enough memory");
            }

            size_t heapSize = freeGpuMem - reservedGPUMemorySize;

            if( Environment::get().MemoryInfo().isSharedMemoryPool() )
            {
                heapSize /= 2;
                PMacc::log< PARATAXISLogLvl::MEMORY > ("Shared RAM between GPU and host detected - using only half of the 'device' memory.");
            }
            else
                PMacc::log< PARATAXISLogLvl::MEMORY > ("RAM is NOT shared between GPU and host.");

            PMacc::log< PARATAXISLogLvl::MEMORY > ("%1% of %2% MiB free memory is reserved. Using %3% MiB as the heap for MallocMC")
                % (reservedGPUMemorySize / MiB) % (freeGpuMem / MiB) % (heapSize / MiB);
            // initializing the heap for particles
            mallocMC::initHeap(heapSize);
            this->mallocMCBuffer = new PMacc::MallocMCBuffer();
        }

    private:
        void checkGridConfiguration(Space globalGridSize, GridLayout layout)
        {
            for(uint32_t i=0; i<simDim; ++i)
            {
                // global size must be a divisor of supercell size
                // note: this is redundant, while using the local condition below
                if(globalGridSize[i] % MappingDesc::SuperCellSize::toRT()[i] != 0)
                    throw std::invalid_argument("Grid size must be a multiple of the supercell size");
                // local size must be a divisor of supercell size
                if(layout.getDataSpaceWithoutGuarding()[i] % MappingDesc::SuperCellSize::toRT()[i] != 0)
                    throw std::invalid_argument("Local grid size must be a multiple of the supercell size");
                // local size must be at least 3 superCells (1x core + 2x border)
                // note: size of border = guard_size (in superCells)
                if(layout.getDataSpaceWithoutGuarding()[i] / MappingDesc::SuperCellSize::toRT()[i] < 3)
                    throw std::invalid_argument("Local grid size must be at least 3 supercells");
            }
        }
    };

}  // namespace parataxis
