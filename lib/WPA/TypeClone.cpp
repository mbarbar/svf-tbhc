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
        idToTypeMap[srcID] = tilde(pag->getPAGNode(srcID)->getType());
        assert(idToTypeMap[srcID] != NULL && "TypeClone: non-heap does not have a type?");
    }

    double end = stat->getClk();
    addrTime += (end - start) / TIMEINTERVAL;

    return changed;
}

bool TypeClone::processCopy(const CopySVFGNode* copy) {
    double start = stat->getClk();

    bool changed;

    if (isCast(copy)) {
        processCast(copy);
    } else {
        bool changed = unionPts(copy->getPAGDstNodeID(), copy->getPAGSrcNodeID());
    }

    double end = stat->getClk();
    copyGepTime += (end - start) / TIMEINTERVAL;

    return changed;
}

bool TypeClone::processCast(const CopySVFGNode *copy) {
    const CastInst *castInst = SVFUtil::dyn_cast<CastInst>(copy->getInst());
    const Type *toType = castInst->getDestTy();

    if (isPod(tilde(toType))) {
        return processPodCast(copy);
    } else {
        return processFancyCast(copy);
    }

}

bool TypeClone::processPodCast(const CopySVFGNode *copy) {
    bool changed = false;

    const CastInst *castInst = SVFUtil::dyn_cast<CastInst>(copy->getInst());
    const Type *toType = castInst->getDestTy();

    NodeID dstId = copy->getPAGDstNodeID();
    PointsTo &srcPts = getPts(copy->getPAGSrcNodeID());

    for (PointsTo::iterator o = srcPts.begin(); o != srcPts.end(); ++o) {
        assert(idToTypeMap.find(*o) != idToTypeMap.end() && "TypeClone: o not allocated!");
        const Type *oType = idToTypeMap[*o];

        if (oType == NULL) {
            // POD-UNDEF-CAST
        } else if (isBase(tilde(toType), oType)) {
            // POD-UPCAST
            changed = changed || addPts(dstId, *o);
        } else if (isBase(oType, tilde(toType))) {
            // POD-DOWNCAST
            // TODO: needs to be checked if it's okay to just change type/label.
            //       because our intention is to "back-update" the metadata,
            //       not create a new object.
            idToTypeMap[*o] = tilde(toType);
            idToCloneNodeMap[*o] = copy->getId();

            changed = changed || addPts(dstId, *o);
        }
    }

    return changed;
}

bool TypeClone::processFancyCast(const CopySVFGNode *copy) {
    bool changed = false;

    const CastInst *castInst = SVFUtil::dyn_cast<CastInst>(copy->getInst());
    const Type *toType = castInst->getDestTy();

    NodeID dstId = copy->getPAGDstNodeID();
    PointsTo &srcPts = getPts(copy->getPAGSrcNodeID());

    for (PointsTo::iterator o = srcPts.begin(); o != srcPts.end(); ++o) {
        assert(idToTypeMap.find(*o) != idToTypeMap.end() && "TypeClone: o not allocated!");
        const Type *oType = idToTypeMap[*o];

        //  CAST-UNDEF      CAST-TYPED
        if (oType == NULL || isBase(tilde(toType), oType)) {
            changed = changed || addPts(dstId, *o);
        } else {
            // DON'T PROPAGATE!
        }
    }

    return changed;
}

bool TypeClone::isCast(const CopySVFGNode *copy) const {
    const Instruction *inst = copy->getInst();
    return inst != NULL && SVFUtil::isa<CastInst>(inst);
}

bool TypeClone::isPod(const Type *t) const {
        // TODO
        return false;
}

bool TypeClone::isBase(const Type *a, const Type *b) const {
        // TODO
        return false;
}

const Type *TypeClone::tilde(const Type *t) const {
    // TODO
    return NULL;
}

