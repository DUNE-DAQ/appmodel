#ifndef APPDALISSUES_HPP
#define APPDALISSUES_HPP

#include "ers/Issue.hpp"

namespace dunedaq {
  ERS_DECLARE_ISSUE(appdal, BadConf, what, ((std::string)what))
  ERS_DECLARE_ISSUE(appdal, BadStreamConf,
                    "Failed to cast stream parameters " << id << " to " << stype,
                    ((std::string)id) ((std::string)stype))
}


#endif // APPDALISSUES_HPP
