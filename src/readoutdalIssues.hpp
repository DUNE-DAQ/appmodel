#ifndef READOUTDALISSUES_HPP
#define READOUTDALISSUES_HPP

#include "ers/Issue.hpp"

namespace dunedaq {
  ERS_DECLARE_ISSUE(readoutdal, BadConf, what, ((std::string)what))
  ERS_DECLARE_ISSUE(readoutdal, BadStreamConf,
                    "Failed to cast stream parameters " << id << " to " << stype,
                    ((std::string)id) ((std::string)stype))
}


#endif // READOUTDALISSUES_HPP
