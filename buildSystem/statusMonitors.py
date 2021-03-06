# Copyright 2015-2016 Alexander Grund
#
# This file is part of ParaTAXIS.
#
# ParaTAXIS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# ParaTAXIS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with ParaTAXIS.  If not, see <http://www.gnu.org/licenses/>.

from execHelpers import execCmd
import re

class BaseMonitor:
    isWaiting = False
    isFinished = False
    
    def update(self):
        pass

class BashMonitor(BaseMonitor):
    def __init__(self, stdout, stderr):
        self.isFinished = True
  
        
class QSubMonitor(BaseMonitor):
    def __init__(self, stdout, stderr):
        if(len(stdout) != 1):
            raise Exception("Unexpected output from qsub command: " + stdout)
        self.jobId = stdout[0]
        self.update()
    
    def update(self):
        if(self.jobId != None):
            res = execCmd("qstat " + self.jobId, True)
            if(res.result == 0):
                if(len(res.stdout) < 2):
                    raise Exception("Unexpected output from qstat: " + str(res.stdout))
                jobStatus = res.stdout[-1].split()
                if(len(jobStatus) != 6):
                    raise Exception("Unexpected output from qstat: " + str(res.stdout))
                jobState = jobStatus[4]
                if(jobState == "C" or jobState == "E"):
                    self.isWaiting = False
                    self.isFinished = True
                elif(jobState == "R" or jobState == "T"):
                    self.isWaiting = False
                    self.isFinished = False
                elif(jobState == "W" or jobState == "Q"):
                    self.isWaiting = True
                    self.isFinished = False
                else:
                    raise Exception("Invalid job state: " + jobState)
            elif("Unknown Job Id" in res.stderr[-1]):
                self.isWaiting = False
                self.isFinished = True
            else:
                raise Exception("Failed to execute qstat: " + str(res.stdout) + "\n" + str(res.stderr))
                
class SBatchMonitor(BaseMonitor):
    def __init__(self, stdout, stderr):
        if(len(stdout) != 1):
            raise Exception("Unexpected output from sbatch command: " + stdout)
        jobRegExp = "Submitted batch job (\\d+)"
        resJobId = re.search(jobRegExp, stdout[0])
        if not resJobId:
            raise Exception("Expected '"+ jobRegExp +"' from sbatch command. : " + stdout[0])
        self.jobId = resJobId.group(1)        
        self.update()
        self.timeoutCt = 0
    
    def update(self):
        if(self.jobId != None):
            res = execCmd("squeue -o \"JobState=%T\" -j " + self.jobId, True)
            if(res.result == 0):
                self.timeoutCt = 0
                if(len(res.stdout) != 2):
                    if len(res.stdout) == 1 and res.stdout[0] == "JobState=STATE":
                        self.isWaiting = False
                        self.isFinished = True   
                    else:                     
                        raise Exception("Unexpected output from squeue: " + str(res.stdout))
                    return
                jobStatRegExp = "JobState=(\w+)"
                jobState = re.match(jobStatRegExp, res.stdout[-1])
                if not jobState:
                    raise Exception("Unexpected output from squeue: " + str(res.stdout))
                jobState = jobState.group(1)
                if(jobState == "CANCELLED" or jobState == "FAILED" or jobState == "COMPLETED" or jobState == "TIMEOUT" or jobState == "PREEMPTED" or jobState == "NODE_FAIL" or jobState == "SPECIAL_EXIT"):
                    self.isWaiting = False
                    self.isFinished = True
                elif(jobState == "RUNNING" or jobState == "COMPLETING"):
                    self.isWaiting = False
                    self.isFinished = False
                elif(jobState == "PENDING" or jobState == "SUSPENDED" or jobState == "CONFIGURING"):
                    self.isWaiting = True
                    self.isFinished = False
                else:
                    raise Exception("Invalid job state: " + jobState)
            elif("Invalid job id specified" in res.stderr[-1]):
                self.isWaiting = False
                self.isFinished = True
            elif("Socket timed out" in res.stderr[-1]):
                self.timeoutCt += 1
                if(self.timeoutCt > 50):
                    raise Exception("Timeout for squeue: " + str(res.stdout) + "\n" + str(res.stderr))
            else:
                raise Exception("Failed to execute squeue(" + str(res.result) + "): " + str(res.stdout) + "\n" + str(res.stderr))
                
def GetMonitor(submitCmd, stdout, stderr):
    if(submitCmd == "bash"):
        return BashMonitor(stdout, stderr)
    elif(submitCmd == "qsub"):
        return QSubMonitor(stdout, stderr)
    elif(submitCmd == "sbatch"):
        return SBatchMonitor(stdout, stderr)
    else:
        raise Exception("Unsupported submit command: `" + submitCmd + "`")

