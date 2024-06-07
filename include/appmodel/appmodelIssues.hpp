#ifndef APPMODELISSUES_HPP
#define APPMODELISSUES_HPP

#include "ers/Issue.hpp"

namespace dunedaq {
  ERS_DECLARE_ISSUE(appmodel, BadConf, what, ((std::string)what))
  ERS_DECLARE_ISSUE(appmodel, BadStreamConf,
                    "Failed to cast stream parameters " << id << " to " << stype,
                    ((std::string)id) ((std::string)stype))
}


#endif // APPMODELISSUES_HPP
