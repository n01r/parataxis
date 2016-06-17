#pragma once

#include "xrtTypes.hpp"
#include "plugins/ISimulationPlugin.hpp"
#include "plugins/hdf5/SplashWriter.hpp"
#include "plugins/hdf5/DataBoxWriter.hpp"
#include "plugins/hdf5/RngStateBoxWriter.hpp"
#include "plugins/hdf5/RngStateBoxReader.hpp"
#include "plugins/hdf5/splashUtils.hpp"
#include "plugins/hdf5/BasePlugin.hpp"
#include "plugins/openPMD/WriteFields.hpp"
#include "plugins/openPMD/LoadFields.hpp"
#include "plugins/openPMD/WriteSpecies.hpp"
#include "plugins/openPMD/LoadSpecies.hpp"
#include "DensityField.hpp"
#include "debug/LogLevels.hpp"
#include <debug/VerboseLog.hpp>
#include <particles/IdProvider.def>
#include <splash/splash.h>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <string>

namespace xrt {
namespace plugins {
namespace hdf5 {

class HDF5Output: public ISimulationPlugin, private hdf5::BasePlugin
{
public:
    HDF5Output(): notifyPeriod(0)
    {
        Environment::get().PluginConnector().registerPlugin(this);
    }

    void pluginRegisterHelp(po::options_description& desc) override
    {
        desc.add_options()
            ((getPrefix() + ".period").c_str(), po::value<uint32_t>(&notifyPeriod)->default_value(0), "enable HDF5 IO [for each n-th step]")
            /* 1,000,000 particles are around 3900 frames at 256 particles per frame
             * and match ~30MiB with typical picongpu particles.
             * The only reason why we use 1M particles per chunk is that we can get a
             * frame overflow in our memory manager if we process all particles in one kernel.
             **/
            ("hdf5.restart-chunkSize", po::value<uint32_t > (&restartChunkSize)->default_value(1000000),
             "Number of particles processed in one kernel call during restart to prevent frame count blowup");
        BasePlugin::pluginRegisterHelp(desc);
    }

    std::string getPrefix() const override
    {
        return "hdf5";
    }

    std::string pluginGetName() const override
    {
        return "HDF5Output";
    }

    void notify(uint32_t currentStep) override
    {
        writeHDF5(currentStep, false);
    }

    void checkpoint(uint32_t currentStep, const std::string checkpointDirectory) override
    {
        this->checkpointDirectory = checkpointDirectory;
        writeHDF5(currentStep, true);
    }

    void restart(uint32_t restartStep, const std::string restartDirectory) override;
private:
    void writeHDF5(uint32_t currentStep, bool isCheckpoint);
    void pluginLoad() override;
    void pluginUnload() override;

    uint32_t notifyPeriod;
    uint32_t restartChunkSize;
    std::string checkpointDirectory;
};

void HDF5Output::writeHDF5(uint32_t currentStep, bool isCheckpoint)
{
    std::string fname;
    if (isCheckpoint)
    {
        if (boost::filesystem::path(checkpointFilename).is_relative())
            fname = checkpointDirectory + "/" + checkpointFilename;
        else
            fname = checkpointFilename;
    }else
        fname = filename;
    openH5File(fname, false);
    auto writer = makeSplashWriter(*dataCollector, currentStep);

    __getTransactionEvent().waitForFinished();

    // Write IdProvider
    const auto idProviderState = PMacc::IdProvider<simDim>::getState();
    PMacc::log<XRTLogLvl::IN_OUT>("HDF5: Writing IdProvider state (StartId: %1%, NextId: %2%, maxNumProc: %3%)")
            % idProviderState.startId % idProviderState.nextId % idProviderState.maxNumProc;
    writer.SetCurrentDataset("picongpu/idProvider/startId");
    splash::Domain globalDomain =
            makeSplashDomain(
                    Space::create(0),
                    Environment::get().GridController().getGpuNodes()
            );
    splash::Domain localDomain =
            makeSplashDomain(
                    Environment::get().GridController().getPosition(),
                    Space::create(1)
            );

    writer.GetDomainWriter()(&idProviderState.startId, simDim, globalDomain, localDomain);
    writer.GetAttributeWriter()("maxNumProc", idProviderState.maxNumProc);

    writer.SetCurrentDataset("picongpu/idProvider/nextId");
    writer.GetDomainWriter()(&idProviderState.nextId, simDim, globalDomain, localDomain);

    auto& dc = Environment::get().DataConnector();
    // Write Random field
    auto& rngBuffer = dc.getData<RNGProvider>(RNGProvider::getName()).getStateBuffer().getHostBuffer();
    if(rngBuffer.getCurrentDataSpace().productOfComponents() > 0)
    {
        writer.SetCurrentDataset("picongpu/rngProvider");
        hdf5::writeDataBox(
                writer,
                rngBuffer.getDataBox(),
                Environment::get().SubGrid().getGlobalDomain(),
                Environment::get().SubGrid().getLocalDomain()
                );
    }
    Environment::get().DataConnector().releaseData(RNGProvider::getName());

    openPMD::WriteFields<DensityField>()(writer);
    openPMD::WriteSpecies<PIC_Photons>()(writer, *cellDescription_);

    // Write this at the end, as we need to annotate the fields which do not exist yet
    openPMD::writeHeader(writer, fname, true);
    closeH5File();
}

void HDF5Output::restart(uint32_t restartStep, const std::string restartDirectory)
{
    std::string fname;
    if (boost::filesystem::path(restartFilename).is_relative())
        fname = restartDirectory + "/" + restartFilename;
    else
        fname = restartFilename;
    openH5File(fname, true);
    auto writer = makeSplashWriter(*dataCollector, restartStep);

    // Read IdProvider
   splash::Domain globalDomain =
            makeSplashDomain(
                    Space::create(0),
                    Environment::get().GridController().getGpuNodes()
            );
    splash::Domain localDomain =
            makeSplashDomain(
                    Environment::get().GridController().getPosition(),
                    Space::create(1)
            );

    PMacc::IdProvider<simDim>::State idProviderState;
    writer.SetCurrentDataset("picongpu/idProvider/startId");
    writer.GetDomainReader()(&idProviderState.startId, simDim, globalDomain, localDomain);
    writer.GetAttributeReader()("maxNumProc", idProviderState.maxNumProc);

    writer.SetCurrentDataset("picongpu/idProvider/nextId");
    writer.GetDomainReader()(&idProviderState.nextId, simDim, globalDomain, localDomain);

    PMacc::log<XRTLogLvl::IN_OUT>("HDF5: Setting IdProvider state (StartId: %1%, NextId: %2%, maxNumProc: %3%)")
            % idProviderState.startId % idProviderState.nextId % idProviderState.maxNumProc;
     PMacc::IdProvider<simDim>::setState(idProviderState);

    auto& dc = Environment::get().DataConnector();
    // Read Random field
    auto& rngProvider = dc.getData<RNGProvider>(RNGProvider::getName(), true);
    auto& rngBuffer = rngProvider.getStateBuffer().getHostBuffer();
    if(rngBuffer.getCurrentDataSpace().productOfComponents() > 0)
    {
        writer.SetCurrentDataset("picongpu/rngProvider");
        hdf5::readDataBox(
                writer,
                rngBuffer.getDataBox(),
                Environment::get().SubGrid().getGlobalDomain(),
                Environment::get().SubGrid().getLocalDomain()
                );
    }
    rngProvider.getStateBuffer().hostToDevice();
    Environment::get().DataConnector().releaseData(RNGProvider::getName());

    openPMD::LoadFields<DensityField>()(writer);
    openPMD::LoadSpecies<PIC_Photons>()(writer, *cellDescription_, restartChunkSize);
    closeH5File();
}

void HDF5Output::pluginLoad()
{
    BasePlugin::pluginLoad();
    if (notifyPeriod > 0)
        Environment::get().PluginConnector().setNotificationPeriod(this, notifyPeriod);
}

void HDF5Output::pluginUnload()
{
    BasePlugin::pluginUnload();
}

}  // namespace hdf5
}  // namespace plugins
}  // namespace xrt
