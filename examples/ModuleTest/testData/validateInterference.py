import sys
import os
import numpy as np
import unittest
from PIL import Image
sys.path.append(os.path.join(os.path.dirname(__file__), "..", "buildSystem"))
from ParamParser import ParamParser
import scatterHelpers as scatter
import bigfloat as bf

class TestInterference(unittest.TestCase):
    def checkCoordinate(self, imgCoord, shouldPos):
        """Check if shouldPos (float value) is the same as imgCoord (int value) using floor and ceil rounding"""
        # Note: Image coordinates are y,x
        if(imgCoord[1] != np.floor(shouldPos[0])):
            self.assertEqual(imgCoord[1], np.ceil(shouldPos[0]))
        if(imgCoord[0] != np.floor(shouldPos[1])):
            self.assertEqual(imgCoord[0], np.ceil(shouldPos[1]))

    def testInterference(self):
        cmakeFlags = os.environ["TEST_CMAKE_FLAGS"].split(" ")
        paramOverwrites = None
        for flag in cmakeFlags:
            if flag.startswith("-DPARAM_OVERWRITES:LIST"):
                paramOverwrites = flag.split("=", 1)[1].split(";")

        params = ParamParser()
        if paramOverwrites:
            for param in paramOverwrites:
                if param.startswith("-D"):
                    param = param[2:].split("=")
                    params.AddDefine(param[0], param[1])
        params.ParseFolder(os.environ["TEST_OUTPUT_PATH"] + "/simulation_defines/param")

        params.SetCurNamespace("xrt::detector::PhotonDetector")
        detector = scatter.DetectorData([], [params.GetNumber("cellWidth"), params.GetNumber("cellHeight")], params.GetNumber("distance"))
        self.assertEqual(params.GetValue("IncomingParticleHandler"), "particleHandlers::AddWaveParticles")

        params.SetCurNamespace("xrt::particles::scatterer::direction::Fixed")
        scatterAngles = [params.GetNumber("angleY"), params.GetNumber("angleZ")]

        params.SetCurNamespace("xrt::initialDensity")
        self.assertEqual(params.GetValue("Generator"), "AvailableGenerators::DoublePoint")
        params.SetCurNamespace("xrt::initialDensity::AvailableGenerators::DoublePoint")
        scatterParticle1 = scatter.ParticleData([params.GetNumber("offsetX"), params.GetNumber("offsetY"), params.GetNumber("offsetZ1")], scatterAngles)
        scatterParticle2 = scatter.ParticleData([params.GetNumber("offsetX"), params.GetNumber("offsetY"), params.GetNumber("offsetZ2")], scatterAngles)

        params.SetCurNamespace("xrt")
        simulation = scatter.SimulationData(list(map(int, os.environ["TEST_GRID_SIZE"].split(" "))),
                                            [params.GetNumber("SI::CELL_WIDTH"), params.GetNumber("SI::CELL_HEIGHT"), params.GetNumber("SI::CELL_DEPTH")])

        pulseLen = np.floor(params.GetNumber("laserConfig::PULSE_LENGTH") / params.GetNumber("SI::DELTA_T"))
        numPartsPerTsPerCell = params.GetNumber("laserConfig::distribution::Const::numParts")
        waveLen = params.GetNumber("wavelengthPhotons", getFromValueIdentifier = True)
        print("Wavelen=", waveLen)

        with open(os.environ["TEST_BASE_BUILD_PATH"] + "/" + os.environ["TEST_NAME"] + "_detector.tif", 'rb') as imFile:
            im = Image.open(imFile)
            detector.resize(im.size)
            
            ## Calculation
            posOnDet1 = scatter.getBinnedDetCellIdx(scatterParticle1, detector, simulation)
            posOnDet2 = scatter.getBinnedDetCellIdx(scatterParticle2, detector, simulation)
            np.testing.assert_allclose(posOnDet1, posOnDet2)

            ## Checks
            imgData = np.array(im)
            whitePts = np.transpose(np.where(imgData > 1.e-3))
            print(len(whitePts), "white pixels at:", whitePts)
            self.assertEqual(len(whitePts), 1)
            self.checkCoordinate(whitePts[0], posOnDet1)
            
            phi1 = scatter.calcPhase(scatterParticle1, detector, simulation, waveLen)
            phi2 = scatter.calcPhase(scatterParticle2, detector, simulation, waveLen)
            real = bf.cos(phi1) + bf.cos(phi2)
            imag = bf.sin(phi1) + bf.sin(phi2)
            intensity = real*real + imag*imag
            intensityPerTs = intensity * numPartsPerTsPerCell
            print("Phis:", phi1, phi2, "Diff", phi1-phi2, "IntensityPerTs", intensityPerTs)
            expectedDetCellValue = float(intensityPerTs * pulseLen)
            self.assertAlmostEqual(imgData[tuple(whitePts[0])], expectedDetCellValue, delta = 0.01)

if __name__ == '__main__':
    unittest.main()
