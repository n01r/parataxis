#!/bin/bash
#
# Copyright 2013-2016 Rene Widera, Alexander Grund
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

##calculations will be performed by tbg##

# settings that can be controlled by environment variables before submit
.TBG_author=${MY_NAME:+--author \"${MY_NAME}\"}

# 4 gpus per node if we need more than 4 gpus else same count as TBG_tasks
.TBG_gpusPerNode=`if [ $TBG_tasks -gt 4 ] ; then echo 4; else echo $TBG_tasks; fi`
    
#number of cores per parallel node / default is 2 cores per gpu on k20 queue
.TBG_coresPerNode="$(( TBG_gpusPerNode * 2 ))"

# use ceil to caculate nodes
.TBG_nodes="$(( ( TBG_tasks + TBG_gpusPerNode -1 ) / TBG_gpusPerNode))"
## end calculations ##

# overwrite .profile
.TBG_profileFile=$TBG_profileFile
profileFile=!TBG_profileFile
profileFile=${profileFile:-"$HOME/picongpu.profile"}

set -o pipefail
echo 'Running program...'

cd !TBG_dstPath

export MODULES_NO_OUTPUT=1
source $profileFile
if [ $? -ne 0 ] ; then
  echo "Error: $profileFile not found!"
  exit 1
fi
unset MODULES_NO_OUTPUT
    
#set user rights to u=rwx;g=r-x;o=---
umask 0027 
    
mkdir simOutput 2> /dev/null
cd simOutput

echo 'mpirun -tag-output --display-map -npernode !TBG_gpusPerNode -n !TBG_tasks !TBG_dstPath/bin/!TBG_program !TBG_author !TBG_programParams'
mpirun -tag-output --display-map -npernode !TBG_gpusPerNode -n !TBG_tasks !TBG_dstPath/bin/!TBG_program !TBG_author !TBG_programParams
