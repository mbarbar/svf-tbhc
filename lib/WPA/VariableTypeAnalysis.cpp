/*
 * VariableTypeAnalysis.cpp
 *
 *  Created on: 24/10/2018
 *      Author: Mohamad Barbar
 */

#include "WPA/VariableTypeAnalysis.h"
#include "MemoryModel/ConsG.h"

void VariableTypeAnalysis::analyze(SVFModule module) {
    PointerAnalysis::initialize(module);
    ConstraintGraph *cg = new ConstraintGraph(pag);

    for (auto nodeI = cg->begin(); nodeI != cg->end(); ++nodeI) {

    }
}
