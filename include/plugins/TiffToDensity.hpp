#pragma once

#include "xrtTypes.hpp"
#include "plugins/ISimulationPlugin.hpp"

#include <tiffWriter/tiffWriter.hpp>

namespace xrt {
namespace plugins {

    class TiffToDensity : public ISimulationPlugin
    {
        std::string name;
        std::string prefix;

        std::string filePath;
        unsigned firstIdx, lastIdx, minSizeFiller, imgOffsetX, imgOffsetY;
        std::vector<unsigned> simOffset;
        char filler;
        int maxImgSize;
        bool repeat;

    public:
        TiffToDensity():
            name("TiffToDensity: Converts one or more tiff images to the density field"),
            prefix("tiff2Dens.")
        {
            Environment::get().PluginConnector().registerPlugin(this);
        }

        void pluginRegisterHelp(po::options_description& desc) override
        {
            desc.add_options()
                ((prefix + "inputFile").c_str(), po::value<std::string>(&filePath), "Input file to use, can contain %i as a placeholder for 3D FFTs")
                ((prefix + "firstIdx").c_str(), po::value<unsigned>(&firstIdx)->default_value(0), "first index to use")
                ((prefix + "lastIdx").c_str(), po::value<unsigned>(&lastIdx)->default_value(0), "last index to use")
                ((prefix + "repeat").c_str(), po::value<bool>(&repeat)->default_value(true), "Repeat single image along X-axis of simulation")
                ((prefix + "minSize").c_str(), po::value<unsigned>(&minSizeFiller)->default_value(0), "Minimum size of the string replaced for %i")
                ((prefix + "fillChar").c_str(), po::value<char>(&filler)->default_value('0'), "Char used to fill the string to the minimum maxImgSize")
                ((prefix + "xStart").c_str(), po::value<unsigned>(&imgOffsetX)->default_value(0), "Offset in x-Direction of image")
                ((prefix + "yStart").c_str(), po::value<unsigned>(&imgOffsetY)->default_value(0), "Offset in y-Direction of image")
                ((prefix + "simOff").c_str(), po::value<std::vector<unsigned>>(&simOffset)->multitoken(), "Offset into the simulation")
                ((prefix + "size").c_str(), po::value<int>(&maxImgSize)->default_value(-1), "Size of the image to use (-1=all)")
                ;
        }

        std::string pluginGetName() const override
        {
            return name;
        }

        void notify(uint32_t currentStep) override
        {
            PMacc::log< XRTLogLvl::IN_OUT >("Loading density field");

            if(firstIdx == lastIdx || filePath.find("%i") == std::string::npos)
                load2D();
            else
                load3D();
        }

       void checkpoint(uint32_t currentStep, const std::string checkpointDirectory) override
       {}
       void restart(uint32_t restartStep, const std::string restartDirectory) override
       {}
    protected:
        void pluginLoad() override
        {
            // If a file is not given, don't register
            if(filePath.empty())
                return;
            // Notify once
            Environment::get().PluginConnector().setNotificationPeriod(this, std::numeric_limits<uint32>::max());
        }

        /// Returns the number as a string extend by the filler to the minSize
        std::string
        getFilledNumber(uint32_t num) const
        {
            std::string s(std::to_string(num));
            while(s.size()<minSizeFiller)
                s = filler + s;
            return s;
        }

        /// Replaces a string in a string
        std::string
        replace(std::string str, const std::string& from, const std::string& to) const
        {
            size_t start_pos = str.find(from);
            if(start_pos == std::string::npos)
                return str;
            return str.replace(start_pos, from.length(), to);
        }

        /// Gets the file path for a given slice number (replaces '%i' in filePath member by the number)
        std::string getFilePath(uint32_t num) const
        {
            return replace(filePath, "%i", getFilledNumber(firstIdx));
        }

        /// Calculates the bounds into the local density field
        /// \param begin First index into the local field
        /// \param end One past the end index into the local field
        /// \param imgOffset offset that needs to be added to the local coords to get the img coords
        /// \param depth Number of slices in 3D (in simulation x-direction)
        /// \return True if anything needs to be copied, false if e.g. begin >= end
        bool calculateBounds(Space& begin, Space& end, Space& imgOffset, int32_t depth)
        {
            auto localDomain = Environment::get().SubGrid().getLocalDomain();
            // Get local size including guards on both sides
            Space localSize = localDomain.size + 2 * cellDescription_ ->getGuardingSuperCells() * SuperCellSize::toRT();
            // Offset into the simulation
            Space simOffset = convertToSpace(this->simOffset, 0, "");
            // Offset from the start of the local domain to the start of the image (can be negative, if the image starts in a previous slice:
            // == difference of sim offset and local offset + the guarding cells at the beginning of the sim
            Space offset = simOffset - localDomain.offset + cellDescription_ ->getGuardingSuperCells() * SuperCellSize::toRT();
            if(simDim == 3)
            {
                // For 3D sims check if our "slice" in x direction is in the range that should be filled
                // and exit early if it is not (avoid loading the image altogether)
                if(offset.x() >= localSize.x())
                    return false; // Starts after this domain
                if(offset.x() + depth <= 0)
                    return false; // Ends before this domain
            }
            tiffWriter::FloatImage<> img(getFilePath(firstIdx), false);
            Space2D imgSize(img.getWidth(), img.getHeight());
            imgOffset = Space(0, this->imgOffsetX, this->imgOffsetY);
            // Because of the offset the image appears smaller
            imgSize -= imgOffset.shrink<2>(1);
            if(maxImgSize >= 0)
            {
                // Use the given the size but stay within the bounds
                imgSize.x() = std::min(imgSize.x(), maxImgSize);
                imgSize.y() = std::min(imgSize.y(), maxImgSize);
            }
            // Get size and offset in the plane
            const Space2D offset2D = offset.shrink<2>(1);
            const Space2D localSize2D = localSize.shrink<2>(1);
            // Get bounds for the local area/volume
            // x >= 0 && && x < depth + offset (see below for y)
            begin.x() = std::max(0, offset.x());
            // x < localSize or for no repeat mode use only 1 slice
            end.x() = std::min(localSize.x(), offset.x() + depth);
            // y >= 0 && y >= offset
            begin.y() = std::max(0, offset2D.x());
            // y < localSize && y - offset < imgSize ( <=> y < imgSize + offset)
            // Remember: y is in local coords, offset is offset to start of image as seen in local coords
            // --> Start of image is at y = offset -> imgIdx = y - offset
            end.y() = std::min(localSize2D.x(), imgSize.x() + offset2D.x());
            // Same as in y
            begin.z() = std::max(0, offset2D.y());
            end.z() = std::min(localSize2D.y(), imgSize.y() + offset2D.y());
            // Check if we need to insert a part of the img (Note: x >= y <=> x > y-1)
            // (e.g. false if simulation is split in y and we only insert into the top part)
            if(begin.isOneDimensionGreaterThan(end - 1))
                return false;
            // Combine both offsets
            imgOffset -= offset;
            return true;
        }

        void load2D()
        {
            Space begin, end, imgOffset;
            if(!calculateBounds(begin, end, imgOffset, repeat ? Environment::get().SubGrid().getGlobalDomain().size.x() : 1))
                return;

            auto& dc = Environment::get().DataConnector();
            auto& densityField = dc.getData<DensityField>(DensityField::getName(), true);
            auto densityBox = densityField.getHostDataBox();
            tiffWriter::FloatImage<> img(getFilePath(firstIdx));
            Space2D imgOffset2D = imgOffset.shrink<2>(1);
            for(int32_t z = begin.z(); z < end.z(); z++)
            {
                for(int32_t y = begin.y(); y < end.y(); y++)
                {
                    for(int32_t x = begin.x(); x < end.x(); x++)
                    {
                        Space idx(x, y, z);
                        Space2D idxImg = idx.shrink<2>(1) + imgOffset2D;
                        densityBox(idx) = img(idxImg.x(), idxImg.y());
                    }
                }
            }
            // Sync to device
            densityField.getGridBuffer().hostToDevice();
            dc.releaseData(DensityField::getName());
        }

        void load3D()
        {
            Space begin, end, imgOffset;
            if(!calculateBounds(begin, end, imgOffset, lastIdx - firstIdx + 1))
                return;

            auto& dc = Environment::get().DataConnector();
            auto& densityField = dc.getData<DensityField>(DensityField::getName(), true);
            auto densityBox = densityField.getHostDataBox();
            Space2D imgOffset2D = imgOffset.shrink<2>(1);
            tiffWriter::FloatImage<> img;
            imgOffset.x() += firstIdx;
            for(int32_t x = begin.x(); x < end.x(); x++)
            {
                img.open(getFilePath(x + imgOffset.x()));
                for(int32_t z = begin.z(); z < end.z(); z++)
                {
                    for(int32_t y = begin.y(); y < end.y(); y++)
                    {
                        Space idx(x, y, z);
                        Space2D idxImg = idx.shrink<2>(1) + imgOffset2D;
                        densityBox(idx) = img(idxImg.x(), idxImg.y());
                    }
                }
            }
            // Sync to device
            densityField.getGridBuffer().hostToDevice();
            dc.releaseData(DensityField::getName());
        }
    };

}  // namespace plugins
}  // namespace xrt