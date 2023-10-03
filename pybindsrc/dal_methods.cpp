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
#include "appdal/TPWriterApplication.hpp"

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


  std::vector<ObjectLocator>
  readout_application_generate(const oksdbinterfaces::Configuration& confdb,
                               const std::string& dbfile,
                               const std::string& app_id,
                               const std::string& session_id) {
    auto app =
      const_cast<oksdbinterfaces::Configuration&>(confdb).get<ReadoutApplication>(app_id);
    auto session =
      const_cast<oksdbinterfaces::Configuration&>(confdb).get<coredal::Session>(session_id);

    std::vector<ObjectLocator> mods;
    for (auto mod : app->generate_modules(
           const_cast<oksdbinterfaces::Configuration*>(&confdb), dbfile, session)) {
      mods.push_back({mod->UID(),mod->class_name()});
    }
    return mods;
  }

  std::vector<ObjectLocator>
  df_application_generate(const oksdbinterfaces::Configuration& confdb,
                          const std::string& dbfile,
                          const std::string& app_id,
                          const std::string& session_id) {
    auto app =
      const_cast<oksdbinterfaces::Configuration&>(confdb).get<DFApplication>(app_id);
    auto session =
      const_cast<oksdbinterfaces::Configuration&>(confdb).get<coredal::Session>(session_id);

    std::vector<ObjectLocator> mods;
    for (auto mod : app->generate_modules(
           const_cast<oksdbinterfaces::Configuration*>(&confdb), dbfile, session)) {
      mods.push_back({mod->UID(),mod->class_name()});
    }
    return mods;
  }


  std::vector<ObjectLocator>
  dfo_application_generate(const oksdbinterfaces::Configuration& confdb,
                           const std::string& dbfile,
                           const std::string& app_id,
                           const std::string& session_id) {
    auto app =
      const_cast<oksdbinterfaces::Configuration&>(confdb).get<DFOApplication>(app_id);
    auto session =
      const_cast<oksdbinterfaces::Configuration&>(confdb).get<coredal::Session>(session_id);

    std::vector<ObjectLocator> mods;
    for (auto mod : app->generate_modules(
           const_cast<oksdbinterfaces::Configuration*>(&confdb), dbfile, session)) {
      mods.push_back({mod->UID(),mod->class_name()});
    }
    return mods;
  }

  std::vector<ObjectLocator>
  tpwriter_application_generate(const oksdbinterfaces::Configuration& confdb,
                                const std::string& dbfile,
                                const std::string& app_id,
                                const std::string& session_id) {
    auto app =
      const_cast<oksdbinterfaces::Configuration&>(confdb).get<TPWriterApplication>(app_id);
    auto session =
      const_cast<oksdbinterfaces::Configuration&>(confdb).get<coredal::Session>(session_id);

    std::vector<ObjectLocator> mods;
    for (auto mod : app->generate_modules(
           const_cast<oksdbinterfaces::Configuration*>(&confdb), dbfile, session)) {
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

  m.def("readout_application_generate", &readout_application_generate, "Generate DaqModules required by ReadoutApplication");
  m.def("df_application_generate", &df_application_generate, "Generate DaqModules required by DFApplication");
  m.def("dfo_application_generate", &dfo_application_generate, "Generate DaqModules required by DFOApplication");
  m.def("tpwriter_application_generate", &tpwriter_application_generate, "Generate DaqModules required by TPWriterApplication");
}

} // namespace dunedaq::appdal::python
