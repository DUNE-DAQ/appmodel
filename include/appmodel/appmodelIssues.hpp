#ifndef APPDALISSUES_HPP
#define APPDALISSUES_HPP

#include "ers/Issue.hpp"

namespace dunedaq {
  ERS_DECLARE_ISSUE(appmodel, BadConf, what, ((std::string)what))
  ERS_DECLARE_ISSUE(appmodel, BadStreamConf,
                    "Failed to cast stream parameters " << id << " to " << stype,
                    ((std::string)id) ((std::string)stype))

  ERS_DECLARE_ISSUE(appmodel,
		    MissingIP,
		    "Daphne configuration has no IP " << ip,
		    ((std::string)ip))
		    
}


#endif // APPDALISSUES_HPP
