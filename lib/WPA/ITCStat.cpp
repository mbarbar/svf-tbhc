//===- ITCStat.cpp -- Statistics of Andersen+ITC analysis------------------//

/*
 * ITCStat.cpp
 *
 *  Created on: Jan 17, 2018
 *      Author: Mohamad Barbar
 */

#include "MemoryModel/ITGraph.h"
#include "MemoryModel/PAG.h"
#include "MemoryModel/PointerAnalysis.h"
#include "WPA/WPAStat.h"
#include "WPA/Andersen.h"

void ITCStat::itGraphStat(){
    PAG *pag = PAG::getPAG();
    ITGraph *itGraph = pta->getITGraph();
    assert((itGraph == pta->getConstraintGraph()) && "itGraph not ITC's constraint graph?");

    PTNumStatMap["NumTypesUsed"] = itGraph->getNumTypes();
    PTNumStatMap["NumBlueprints"] = itGraph->getNumBlueprints();
    PTNumStatMap["NumInstances"] = itGraph->getNumInstances();

    int incompatibleObjCount = 0;
    int otherObjCount = 0;
    for (auto nodeI = itGraph->begin(); nodeI != itGraph->end(); ++nodeI) {
        if (SVFUtil::isa<IncompatibleObjPN>(pag->getPAGNode(nodeI->first))) ++incompatibleObjCount;
        else if (SVFUtil::isa<ObjPN>(pag->getPAGNode(nodeI->first))) ++otherObjCount;
    }

    PTNumStatMap["NumIncompatibleObj"] = incompatibleObjCount;
    PTNumStatMap["NumOtherObj"] = otherObjCount;

    PTAStat::printStat("Incompatible Type Graph Stats");
}

void ITCStat::performStat() {
    AndersenStat::performStat();
    itGraphStat();
}

