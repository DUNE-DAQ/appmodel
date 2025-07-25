/**
 * @file methods.cpp
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "pybind11/operators.h"
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"

#include "logging/Logging.hpp"

namespace py = pybind11;

namespace dunedaq::appmodel::python {

  void hello_world() {
    TLOG() << "Hello, world!";
  }

  void
  register_methods(py::module& m)
  {
    m.def("hello_world", &hello_world, "Print a familiar greeting");
  }

} // namespace dunedaq::appmodel::python
