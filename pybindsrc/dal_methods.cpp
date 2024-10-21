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

#include "appmodel/DFApplication.hpp"
#include "appmodel/DFOApplication.hpp"
#include "appmodel/ReadoutApplication.hpp"
#include "appmodel/TriggerApplication.hpp"
#include "appmodel/FakeHSIApplication.hpp"
#include "appmodel/TPStreamWriterApplication.hpp"
#include "appmodel/HSIEventToTCApplication.hpp"
#include "appmodel/MLTApplication.hpp"
#include "appmodel/WIECApplication.hpp"

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

  template <typename ApplicationType>
  std::vector<ObjectLocator>
  application_generate_template(const conffwk::Configuration& confdb,
                                const std::string& dbfile,
                                const std::string& app_id,
                                const std::string& session_id)
  {
    auto app =
      const_cast<conffwk::Configuration&>(confdb).get<ApplicationType>(app_id);
    auto session =
      const_cast<conffwk::Configuration&>(confdb).get<confmodel::Session>(session_id);

    std::vector<ObjectLocator> mods;
    for (auto mod : app->generate_modules(
           const_cast<conffwk::Configuration*>(&confdb), dbfile, session)) {
      mods.push_back({mod->UID(),mod->class_name()});
    }
    return mods;
  }

  std::vector<std::string> smart_daq_application_construct_commandline_parameters(const conffwk::Configuration& db,
                                                                                  const std::string& session_id,
                                                                                  const std::string& app_id) {
    const auto* app = const_cast<conffwk::Configuration&>(db).get<dunedaq::appmodel::SmartDaqApplication>(app_id);
    const auto* session = const_cast<conffwk::Configuration&>(db).get<dunedaq::confmodel::Session>(session_id);
    return app->construct_commandline_parameters(db, session);
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
  m.def("fakehsi_application_generate", &application_generate_template<FakeHSIApplication>, "Generate DaqModules required by FakeHSIApplication");
  m.def("hsieventtotc_application_generate", &application_generate_template<HSIEventToTCApplication>, "Generate DaqModules required by HSIEventToTCApplication");
  m.def("mlt_application_generate", &application_generate_template<MLTApplication>, "Generate DaqModules required by MLTApplication");
  m.def("wiec_application_generate", &application_generate_template<WIECApplication>, "Generate DaqModules required by WIECApplication");

  m.def("smart_daq_application_construct_commandline_parameters", &smart_daq_application_construct_commandline_parameters, "Get a version of the command line agruments parsed");
}

} // namespace dunedaq::appmodel::python
