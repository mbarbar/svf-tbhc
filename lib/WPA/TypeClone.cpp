/*
 * TypeClone.cpp
 *
 *  Created on: Apr 09, 2019
 *      Author: Mohamad Barbar
 */

#include "WPA/TypeClone.h"
#include "WPA/WPAStat.h"

bool TypeClone::processAddr(const AddrSVFGNode* addr) {
    double start = stat->getClk();

    NodeID srcID = addr->getPAGSrcNodeID();
    /// TODO: see FieldSensitive::processAddr
    if (isFieldInsensitive(srcID)) {
        srcID = getFIObjNode(srcID);
    }

    bool changed = addPts(addr->getPAGDstNodeID(), srcID);

    // Should not have a type, not even undefined.
    assert(idToTypeMap.find(srcID) == idToTypeMap.end() && "TypeClone: already has type!");
    if (isHeapMemObj(srcID)) {
        // Heap objects are initialised with no types.
        idToTypeMap[srcID] = NULL;
    } else {
        idToTypeMap[srcID] = pag->getPAGNode(srcID)->getType();
        assert(idToTypeMap[srcID] != NULL && "TypeClone: non-heap does not have a type?");
    }

    double end = stat->getClk();
    addrTime += (end - start) / TIMEINTERVAL;

    return changed;
}


