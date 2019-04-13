/*
 * TypeClone.cpp
 *
 *  Created on: Apr 09, 2019
 *      Author: Mohamad Barbar
 */

#include "MemoryModel/CHA.h"
#include "WPA/TypeClone.h"
#include "WPA/WPAStat.h"
#include "Util/CPPUtil.h"

const std::string TypeClone::UNDEF_TYPE = "";
const std::string TypeClone::VOID_TYPE = "i8";

void TypeClone::initialize(SVFModule svfModule) {
    this->svfModule = svfModule;
    chg = new CHGraph(svfModule);
    chg->buildCHG();

    findNonPodTypes();

    FlowSensitive::initialize(svfModule);
}

bool TypeClone::processAddr(const AddrSVFGNode* addr) {
    double start = stat->getClk();

    NodeID srcID = addr->getPAGSrcNodeID();
    /// TODO: see FieldSensitive::processAddr
    if (isFieldInsensitive(srcID)) {
        srcID = getFIObjNode(srcID);
    }

    bool changed = addPts(addr->getPAGDstNodeID(), srcID);

    // Should not have a type, not even undefined.
    // TODO: for some reason, this is happening twice.
    //assert(idToTypeMap.find(srcID) == idToTypeMap.end() && "TypeClone: already has type!");
    if (isHeapMemObj(srcID)) {
        // Heap objects are initialised with no types.
        idToTypeMap[srcID] = UNDEF_TYPE;
        idToAllocNodeMap[srcID] = addr->getId();
    } else {
        idToTypeMap[srcID] = tilde(cppUtil::getNameFromType(pag->getPAGNode(srcID)->getType()));
        assert(idToTypeMap[srcID] != UNDEF_TYPE && "TypeClone: non-heap does not have a type?");
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
    TypeStr toType = cppUtil::getNameFromType(castInst->getDestTy());
    TypeStr fromType = cppUtil::getNameFromType(castInst->getSrcTy());

    llvm::outs() << "from: " << fromType << " was " << *(castInst->getSrcTy()) << "\n";
    llvm::outs() << "to: " << toType << " was " << *(castInst->getDestTy()) << "\n";

    if (isPod(tilde(toType))) {
        return processPodCast(copy);
    } else {
        return processFancyCast(copy);
    }

}

bool TypeClone::processGep(const GepSVFGNode* edge) {
    double start = stat->getClk();
    bool changed = false;
    const PointsTo& srcPts = getPts(edge->getPAGSrcNodeID());

    const Instruction *inst = edge->getInst();
    TypeStr srcType = UNDEF_TYPE;
    TypeStr dstType = UNDEF_TYPE;
    if (inst == NULL) {
        // TODO TODO TODO: what to do?
    } else {
        const GetElementPtrInst *gepInst = SVFUtil::dyn_cast<GetElementPtrInst>(inst);
        TypeStr srcType = cppUtil::getNameFromType(gepInst->getSourceElementType());
        TypeStr dstType = cppUtil::getNameFromType(gepInst->getResultElementType());
    }




    if (inst != NULL) {
        llvm::outs() << *(edge->getInst()) << "!!\n";
    } else {
        llvm::outs() << "NULL!!\n";
    }

    PointsTo tmpDstPts;
    for (PointsTo::iterator piter = srcPts.begin(); piter != srcPts.end(); ++piter) {
        NodeID ptd = *piter;
        if (isBlkObjOrConstantObj(ptd))
            tmpDstPts.set(ptd);
        else {
            if (SVFUtil::isa<VariantGepPE>(edge->getPAGEdge())) {
                setObjFieldInsensitive(ptd);
                tmpDstPts.set(getFIObjNode(ptd));
            }
            else if (const NormalGepPE* normalGep = SVFUtil::dyn_cast<NormalGepPE>(edge->getPAGEdge())) {
                assert(idToTypeMap.find(ptd) != idToTypeMap.end() && "TypeClone: no type, not allocated?");
                if (!isBase(tilde(srcType), idToTypeMap[ptd])) {
                    // Incompatibility!
                    continue;
                }

                NodeID fieldSrcPtdNode = getGepObjNode(ptd,	normalGep->getLocationSet());
                // Give the object its attributes in case it was just created.
                idToTypeMap[fieldSrcPtdNode] = tilde(dstType);
                // It is not ideal that we might keep changing the alloc and
                // clone points, but it doesn't matter because it's consistent
                // for everyone that points to fieldSrcPtdNode.
                idToAllocNodeMap[fieldSrcPtdNode] = edge->getId();
                idToCloneNodeMap[fieldSrcPtdNode] = edge->getId();

                tmpDstPts.set(fieldSrcPtdNode);
            }
            else
                assert(false && "new gep edge?");
        }
    }

    if (unionPts(edge->getPAGDstNodeID(), tmpDstPts))
        changed = true;

    double end = stat->getClk();
    copyGepTime += (end - start) / TIMEINTERVAL;
    return changed;
}

bool TypeClone::processPodCast(const CopySVFGNode *copy) {
    bool changed = false;

    const CastInst *castInst = SVFUtil::dyn_cast<CastInst>(copy->getInst());
    TypeStr toType = cppUtil::getNameFromType(castInst->getDestTy());

    NodeID dstId = copy->getPAGDstNodeID();
    PointsTo &srcPts = getPts(copy->getPAGSrcNodeID());

    for (PointsTo::iterator o = srcPts.begin(); o != srcPts.end(); ++o) {
        // TODO: some objects generated - unclear from where, maybe gep.
        // assert(idToTypeMap.find(*o) != idToTypeMap.end() && "TypeClone: o not allocated!");
        TypeStr oType = idToTypeMap[*o];

        if (oType == UNDEF_TYPE) {
            // POD-UNDEF-CAST
            NodeID clone = pag->addDummyObjNode();
            idToTypeMap[clone] = tilde(toType);
            idToCloneNodeMap[clone] = copy->getId();
            idToAllocNodeMap[clone] = idToAllocNodeMap[*o];

            // Add the clone, remove the undefined-type object
            changed = changed || addPts(dstId, clone);
            // Removing should not affect changed...
            getPts(dstId).reset(*o);
        } else if (isBase(tilde(toType), oType) || tilde(toType) == VOID_TYPE) {
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
    TypeStr toType = cppUtil::getNameFromType(castInst->getDestTy());

    NodeID dstId = copy->getPAGDstNodeID();
    PointsTo &srcPts = getPts(copy->getPAGSrcNodeID());

    for (PointsTo::iterator o = srcPts.begin(); o != srcPts.end(); ++o) {
        assert(idToTypeMap.find(*o) != idToTypeMap.end() && "TypeClone: o not allocated!");
        TypeStr oType = idToTypeMap[*o];

        //  CAST-UNDEF              CAST-TYPED
        if (oType == UNDEF_TYPE || (isBase(tilde(toType), oType) || tilde(toType) == VOID_TYPE) {
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

bool TypeClone::isPod(TypeStr t) const {
    return nonPodTypes.find(t) == nonPodTypes.end();
}

bool TypeClone::isBase(TypeStr a, TypeStr b) const {
    if (a == b) return true;

    if (chg->getNode(a) == NULL || chg->getNode(b) == NULL) return false;

    const CHGraph::CHNodeSetTy& aChildren = chg->getInstancesAndDescendants(a);
    const CHNode *bNode = chg->getNode(b);
    // If b is in the set of a's children, then a is a base type of b.
    return aChildren.find(bNode) != aChildren.end();
}

TypeClone::TypeStr TypeClone::tilde(TypeStr t) const {
    // TODO
    return t;
}

void TypeClone::findNonPodTypes(void) {
    Function* fun = NULL;
    for (u32_t i = 0; i < svfModule.getModuleNum(); ++i) {
        Module *mod = svfModule.getLLVMModuleSet()->getModule(i);
        llvm::Module::FunctionListType &functions = mod->getFunctionList();

        for (llvm::Module::FunctionListType::iterator fnI = functions.begin(); fnI != functions.end(); ++fnI) {
            std::string fnMangledName = fnI->getName();
            if (cppUtil::isConstructor(&(*fnI))) {
                // The type has a constructor, so it is not a POD.
                cppUtil::DemangledName fnName = cppUtil::demangle(fnMangledName);
                // TODO: this includes the template - are we keeping them?
                nonPodTypes.insert(fnName.className);
            }
        }
    }
}

