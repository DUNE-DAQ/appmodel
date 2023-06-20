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

#include "readoutdal/ReadoutApplication.hpp"

#include <sstream>

namespace py = pybind11;

namespace dunedaq::readoutdal::python {

  struct ObjectLocator {
    ObjectLocator(const std::string& id_arg, const std::string& class_name_arg) :
      id(id_arg), class_name(class_name_arg) 
      {}
    const std::string id;
    const std::string class_name;
  };


  std::vector<ObjectLocator>
  readout_application_generate(const oksdbinterfaces::Configuration& confdb,
                               const std::string& dbfile,
                               const std::string& app_id) {
    auto app =
      const_cast<oksdbinterfaces::Configuration&>(confdb).get<ReadoutApplication>(app_id);
    std::vector<ObjectLocator> mods;
    for (auto mod : app->generate_modules(
           const_cast<oksdbinterfaces::Configuration*>(&confdb), dbfile)) {
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

  m.def("readout_application_generate", &readout_application_generate, "Generate DaqModules required by application");
}

} // namespace dunedaq::readoutdal::python
