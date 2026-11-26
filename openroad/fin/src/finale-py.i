// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022-2025, The OpenROAD Authors

%{
#include "ord/OpenRoad.hh"
#include "fin/Finale.h"

%}

%include "../../Exception-py.i"
%include "std_string.i"
%include "std_vector.i"
%template(FinLayerNames) std::vector<std::string>;
%include "fin/Finale.h"
