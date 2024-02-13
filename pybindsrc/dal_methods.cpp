/**
 * @file dal_methods.cpp
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "pybind11/operators.h"
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"

#include "coredal/DaqModule.hpp"
#include "coredal/Session.hpp"

#include "appdal/DFApplication.hpp"
#include "appdal/DFOApplication.hpp"
#include "appdal/ReadoutApplication.hpp"
#include "appdal/TriggerApplication.hpp"
#include "appdal/TPStreamWriterApplication.hpp"

#include <sstream>

namespace py = pybind11;

namespace dunedaq::appdal::python {

  struct ObjectLocator {
    ObjectLocator(const std::string& id_arg, const std::string& class_name_arg) :
      id(id_arg), class_name(class_name_arg) 
      {}
    const std::string id;
    const std::string class_name;
  };

  template <typename ApplicationType>
  std::vector<ObjectLocator>
  application_generate_template(const oksdbinterfaces::Configuration& confdb,
                                const std::string& dbfile,
                                const std::string& app_id,
                                const std::string& session_id) 
  {
    auto app =
      const_cast<oksdbinterfaces::Configuration&>(confdb).get<ApplicationType>(app_id);
    auto session =
      const_cast<oksdbinterfaces::Configuration&>(confdb).get<coredal::Session>(session_id);

    std::vector<ObjectLocator> mods;
    for (auto mod : app->generate_modules(dbfile, session)) {
      mods.push_back({mod->UID(),mod->class_name()});
    }
    return mods;
  }

void
register_dal_methods(py::module& m)
{
  py::class_<ObjectLocator>(m, "ObjectLocator")
    .def(py::init<const std::string&, const std::string&>())
    .def_readonly("id", &ObjectLocator::id)
    .def_readonly("class_name", &ObjectLocator::class_name)
    ;

  m.def("readout_application_generate", &application_generate_template<ReadoutApplication>, "Generate DaqModules required by ReadoutApplication");
  m.def("df_application_generate", &application_generate_template<DFApplication>, "Generate DaqModules required by DFApplication");
  m.def("dfo_application_generate", &application_generate_template<DFOApplication>, "Generate DaqModules required by DFOApplication");
  m.def("tpwriter_application_generate", &application_generate_template<TPStreamWriterApplication>, "Generate DaqModules required by TPStreamWriterApplication");
  m.def("trigger_application_generate", &application_generate_template<TriggerApplication>, "Generate DaqModules required by TriggerApplication");
}

} // namespace dunedaq::appdal::python
