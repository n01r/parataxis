#!/usr/bin/python3 -u

import argparse
import sys
import os
import multiprocessing
import shutil
from distutils.util import strtobool
from termHelpers import cprint, thumbsUp, thumbsDown
from execHelpers import execCmd
import Example

def getExampleFolders(exampleNameOrFolder, getAll):
    """Return list of absolute paths to the examples
    
       getAll == False -- The example specified by name or folder
       else            -- All examples in the folder
    """
    if(getAll):
        if(not os.path.isdir(exampleNameOrFolder)):
            cprint("Path to example does not exist: " + exampleNameOrFolder, "red")
            sys.exit(1)
        exampleDirs = os.listdir(exampleNameOrFolder)
        parentDir = exampleNameOrFolder
    elif(os.path.isdir(exampleNameOrFolder)):
        exampleDirs = [exampleNameOrFolder]
        parentDir = "."
    else:
        exampleFolder = "examples/" + exampleNameOrFolder
        if(os.path.isdir(exampleFolder)):
            exampleDirs = [exampleFolder]
            parentDir = "."
        else:
            cprint("Path to example or example does not exist: " + exampleNameOrFolder, "red")
            sys.exit(1)
    result = [os.path.abspath(parentDir + "/" + dir) for dir in exampleDirs]
    return result
    
def writeListToFile(lstIn, file):
    """Write a list into an open file"""
    for el in lstIn:
        file.write("%s\n" % el)

def writeStatusToFiles(result, folder):
    """Write status of an example to files
    
    Write stdout.log, stderr.log, returnCode.txt and cmakeSettings.log (if CMakeCache.txt exists)
    """
    if(not os.path.isdir(folder)):
        return
    with open(folder + "/stdout.log", "w") as file:
        writeListToFile(result.stdout, file)
    with open(folder + "/stderr.log", "w") as file:
        writeListToFile(result.stderr, file)
    with open(folder + "/returnCode.txt", "w") as file:
        file.write("%d" % result.result)
    cmakeSettingsPath = os.path.abspath(folder + "/cmakeSettings.log")
    if(os.path.isfile(cmakeSettingsPath)):
        os.remove(cmakeSettingsPath)
    if(os.path.isfile(folder + "/CMakeCache.txt")):
        execCmd('cd "' + folder + '" && cmake -L . &> "' + cmakeSettingsPath + '"', True)

def doCompile(compilation, args):
    """Actually compile a compilation and do some state keeping (write status files)"""
    result = compilation.configAndCompile(*args)
    if result != None:
        writeStatusToFiles(result, compilation.getBuildPath())
    return result == None or result.result == 0

def compileWorker(input, output):
    """Worker function that processes compilations from input queue and writes result (bool) to output queue"""
    for compilation, args in iter(input.get, 'STOP'):
        try:
            result = doCompile(compilation, args)
            output.put((result, compilation))
        except Exception as e:
            cprint("Error during compilation: " + str(e), "red")
            output.put((False, compilation))
        
def processCompilations(compilations, srcDir, dryRun, verbose, numParallel):
    """Compile the compilations passed
    
    srcDdir     -- Path to folder with CMakeLists.txt
    dryRun      -- Show only commands
    verbose     -- Verbose output
    numParallel -- Execute in parallel using N processes
    Return number of errors
    """
    numErrors = 0
    if(numParallel < 2):
        for c in compilations:
            result = doCompile(c, (srcDir, dryRun, verbose, False))
            if(not result):
                numErrors += 1
    else:
        taskQueue = multiprocessing.Queue()
        doneQueue = multiprocessing.Queue()

        for c in compilations:
            taskQueue.put((c, (srcDir, dryRun, verbose, True)))

        for i in range(numParallel):
            multiprocessing.Process(target=compileWorker, args=(taskQueue, doneQueue)).start()
            taskQueue.put('STOP')
        
        for i in range(len(compilations)):
            (res, compilation) = doneQueue.get() # Potentially blocks
            # Write back result into original compilation (subprocess uses and returns a copy)
            found = False
            for c in compilations:
                if c.getConfig() == compilation.getConfig():
                    c.lastResult = compilation.lastResult
                    found = True
                    break
            if not found:
                raise Exception("Did not find compilation " + str(compilation.getConfig()))
            if(not res):
                numErrors += 1
    return numErrors
    
def printFailures(compilations = None, runtimeTests = None):
    if compilations:
        print("Compile tests:")
        for compilation in compilations:
            if compilation.lastResult != None and compilation.lastResult.result != 0:
                print("\t" + str(compilation) + ": " + compilation.getBuildPath())
    if runtimeTests:
        print("Runtime tests:")
        for test in runtimeTests:
            if not test.lastResult:
                folder = test.getOutputPath(False)
                if folder != None:
                    folder = ": " + folder
                else:
                    folder = ""
                print("\t" + str(test) + folder)

def main(argv):
    srcDir = os.path.abspath(os.path.dirname(__file__) + "../..")
    if not os.path.isfile(srcDir + "/CMakeLists.txt"):
        cprint("Unexpected file location. CMakeLists.txt not found in " + srcDir, "red")
        return 1
    exampleFolder = srcDir + '/examples'
    # Parse the command line.
    parser = argparse.ArgumentParser(description="Compile and run tests", formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-e', '--example', default=exampleFolder, help='Example name, path to one example or example folder (if --all is used)')
    parser.add_argument('-o', '--output', default="testBuilds", help='Path to directory with build directories. If a build directory does not exist, the example will be build automatically')
    parser.add_argument('-a', '--all', action='store_true', help='Process all examples in the folder')
    parser.add_argument('-j', type=int, default=1, const=-1, nargs='?', help='Compile in parallel using N processes', metavar='N')
    parser.add_argument('-k', action='store_true', help='Submit all tests at once before waiting for their completion (usefull for batch systems)')
    parser.add_argument('-d', '--dry-run', action='store_true', help='Just print commands and exit')
    parser.add_argument('-t', '--test', action='append', const="+", nargs='?', help='Compile and execute only tests with given names. Without names it compiles only compilations required by runtime tests')
    parser.add_argument('-p', '--profile-file', help='Specifies the profile file used to set up the environment (e.g. ~/picongpu.profile)')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')
    parser.add_argument('--compile-only', action='store_true', help='Run only compile tests (do not run compiled programs)')
    parser.add_argument('--no-install-clean', action='store_true', help='Do not delete install folders before compiling')
    options = parser.parse_args(argv)
    if options.j < 0:
        options.j = max(1, min(multiprocessing.cpu_count(), 8))
    if options.example == exampleFolder and not options.all and len(argv) > 0:
        sys.stdout.write("Path to default example folder given.\n\nShall I process all examples in that folder? [y/n]")
        options.all = strtobool(input().lower())
    if len(argv) == 0:
        sys.stdout.write("No parameters given. Taking default:\nLoad all examples from " + options.example + ", compile and run them.\n\nContinue? [y/n]")
        if not strtobool(input().lower()):
            return 1
        options.all = True
    
    # Prepare
    ############################################################################
    cprint("Getting examples...", "yellow")
    exampleDirs = getExampleFolders(options.example, options.all)
    if(len(exampleDirs) == 0):
        cprint("No examples found", "red")
        return 1
    cprint("Loading examples...", "yellow")
    examples = Example.loadExamples(exampleDirs, options.profile_file)
    if(examples == None):
        return 1
    compilations = Example.getCompilations(examples, options.output, options.test)
    if not options.no_install_clean:
        cprint("Cleaning install directories", "yellow")
        for c in compilations:
            print("\t" + c.getInstallPath())
            shutil.rmtree(c.getInstallPath(), True)
    # Compile
    ############################################################################
    if options.j > 1:
        cprint("Compiling examples using up to " + str(options.j) + " processes...", "yellow")
    else:
        cprint("Compiling examples...", "yellow")
    numErrors = processCompilations(compilations, srcDir, options.dry_run, options.verbose, options.j)
    if(numErrors > 0):
        cprint(str(numErrors) + " compile errors occured!", "red")
        printFailures(compilations = compilations)
        thumbsDown()
        return 1
    if(options.compile_only):
        cprint(str(len(compilations)) + " compile tests finished!", "green")    
        return 0
    # Run
    ############################################################################
    cprint("Running examples...", "yellow")
    runtimeTests = Example.getRuntimeTests(examples, options.test)
    errorTests = []
    if options.k:
        startedTests = []
        for test in runtimeTests:
            if test.startTest(srcDir, options.output, options.dry_run, options.verbose) != 0:
                numErrors += 1
            else:
                startedTests.append(test)
        for test in startedTests:
            if test.finishTest(options.dry_run, options.verbose) != 0:
                numErrors += 1   
    else:
        for test in runtimeTests:
            if test.execute(srcDir, options.output, options.dry_run, options.verbose) != 0:
                numErrors += 1
    if(numErrors > 0):
        cprint(str(numErrors) + " run errors occured!", "red")
        printFailures(runtimeTests = runtimeTests)
        thumbsDown()
        return 1
    else:
        thumbsUp()
        return 0
    
if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

