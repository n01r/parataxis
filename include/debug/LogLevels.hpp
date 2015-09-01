#pragma once

#include <stdint.h>
#include <debug/VerboseLogMakros.hpp>

namespace xrt{

    #ifndef XRT_VERBOSE_LVL
    #   define XRT_VERBOSE_LVL 0
    #endif

    /*create verbose class*/
    DEFINE_VERBOSE_CLASS(XRTLogLvl)
    (
        /* define log lvl for later use
         * e.g. log<PMaccLogLvl::NOTHING>("TEXT");*/
        DEFINE_LOGLVL(0, NOTHING);
        DEFINE_LOGLVL(1 << 1, SIM_STATE);
        DEFINE_LOGLVL(1 << 2, MEMORY);
        DEFINE_LOGLVL(1 << 3, DOMAINS);
        DEFINE_LOGLVL(1 << 4, IN_OUT);
        DEFINE_LOGLVL(1 << 5, PLUGINS);
    )
    /*set default verbose lvl (integer number)*/
    (NOTHING::lvl|XRT_VERBOSE_LVL);


} // namespace xrt