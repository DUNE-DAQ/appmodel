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

#include "confmodel/DaqModule.hpp"
#include "confmodel/Session.hpp"

#include "appmodel/SmartDaqApplication.hpp"

#include <sstream>

namespace py = pybind11;

namespace dunedaq::appmodel::python {

  struct ObjectLocator {
    ObjectLocator(const std::string& id_arg, const std::string& class_name_arg) :
      id(id_arg), class_name(class_name_arg)
      {}
    const std::string id;
    const std::string class_name;
  };

  std::vector<ObjectLocator>
  smart_daq_application_generate_modules(const conffwk::Configuration& confdb, const std::string& app_id, const std::string& session_id)
  {
    auto app =
      const_cast<conffwk::Configuration&>(confdb).get<appmodel::SmartDaqApplication>(app_id);
    auto session =
      const_cast<conffwk::Configuration&>(confdb).get<confmodel::Session>(session_id);

    auto helper = std::make_shared<ConfigurationHelper>(session);
    app->generate_modules(helper);
    std::vector<ObjectLocator> mods;
    for (auto mod : app->get_modules()) {
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

  m.def("smart_daq_application_generate_modules", &smart_daq_application_generate_modules, "Generate DaqModules");
}

} // namespace dunedaq::appmodel::python
