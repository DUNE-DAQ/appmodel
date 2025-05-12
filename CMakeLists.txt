cmake_minimum_required(VERSION 3.12)
project(appmodel VERSION 3.2.0)

find_package(daq-cmake REQUIRED)

daq_setup_environment()

find_package(okssystem REQUIRED)
find_package(oks REQUIRED)
find_package(oksdalgen REQUIRED)
find_package(ers REQUIRED)
find_package(logging REQUIRED)
find_package(conffwk REQUIRED)
find_package(confmodel REQUIRED)
find_package(fmt REQUIRED)

find_package(Boost COMPONENTS unit_test_framework program_options REQUIRED)

daq_add_dal_library(application.schema.xml hermes.schema.xml fdmodules.schema.xml trigger.schema.xml wiec.schema.xml PDS.schema.xml
  NAMESPACE dunedaq::appmodel DEP_PKGS confmodel
  SOURCES ReadoutApplication.cpp SmartDaqApplication.cpp DFApplication.cpp DFOApplication.cpp
  TPWriterApplication.cpp FakeDataApplication.cpp FakeHSIApplication.cpp DTSHSIApplication.cpp TriggerApplication.cpp TPReplayApplication.cpp MLTApplication.cpp HSIEventToTCApplication.cpp WIECApplication.cpp DaphneApplication.cpp SocketSenderApplication.cpp
  LINK_LIBRARIES conffwk::conffwk fmt::fmt logging::logging oks::oks ers::ers )

daq_add_application(getAppsArguments get_apps_arguments.cxx
  LINK_LIBRARIES confmodel::confmodel appmodel_dal conffwk::conffwk)

daq_add_python_bindings(*.cpp LINK_LIBRARIES appmodel_dal confmodel::confmodel)

daq_add_application(generate_modules_test generate_modules_test.cxx
 TEST LINK_LIBRARIES appmodel_dal confmodel::confmodel conffwk::conffwk
 logging::logging)

daq_add_application(print_detailed_config_info print_detailed_config_info.cxx
 TEST LINK_LIBRARIES appmodel_dal confmodel::confmodel conffwk::conffwk
 logging::logging)

daq_install()
