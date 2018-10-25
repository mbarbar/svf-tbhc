/*
 * VariableTypeAnalysis.cpp
 *
 *  Created on: 24/10/2018
 *      Author: Mohamad Barbar
 */

#include "WPA/VariableTypeAnalysis.h"
#include "MemoryModel/ConsG.h"

void VariableTypeAnalysis::initialize(SVFModule module) {
    PointerAnalysis::initialize(module);
}

void VariableTypeAnalysis::analyze(SVFModule module) {
    for (auto nodeI = cg->begin(); nodeI != cg->end(); ++nodeI) {

    }
}
