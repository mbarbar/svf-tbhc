//===- TypeFilter.cpp -- Type filter/type-based heap cloning base ------------//

/*
 * TypeFilter.cpp
 *
 *  Created on: Feb 08, 2020
 *      Author: Mohamad Barbar
 */

#include "Util/TypeFilter.h"

const DIType *TypeFilter::undefType = nullptr;

TypeFilter::TypeFilter(PointerAnalysis *pta) {
    this->pta = pta;
}

void TypeFilter::setDCHG(DCHGraph *dchg) {
    this->dchg = dchg;
}

void TypeFilter::setPAG(PAG *pag) {
    ppag = pag;
}

bool TypeFilter::isBlkObjOrConstantObj(NodeID o) const {
    if (isClone(o)) o = cloneToOriginalObj.at(o);
    return SVFUtil::isa<ObjPN>(ppag->getPAGNode(o)) && ppag->isBlkObjOrConstantObj(o);
}

bool TypeFilter::isBase(const DIType *a, const DIType *b) const {
    assert(dchg && "TF: DCHG not set!");
    return dchg->isBase(a, b, true);
}

bool TypeFilter::isClone(NodeID o) const {
    return cloneToOriginalObj.find(o) != cloneToOriginalObj.end();
}

void TypeFilter::setType(NodeID o, const DIType *t) {
    objToType.insert({o, t});
}

const DIType *TypeFilter::getType(NodeID o) const {
    assert(objToType.find(o) != objToType.end() && "TF: object has no type?");
    return objToType.at(o);
}

void TypeFilter::setAllocationSite(NodeID o, NodeID site) {
    objToAllocation.insert({o, site});
}

NodeID TypeFilter::getAllocationSite(NodeID o) const {
    assert(objToAllocation.find(o) != objToAllocation.end() && "TF: object has no allocation site?");
    return objToAllocation.at(o);
}

const NodeBS TypeFilter::getObjsWithClones(void) {
    NodeBS objs;
    for (std::pair<NodeID, NodeBS> oc : objToClones) {
        objs.set(oc.first);
    }

    return objs;
}

void TypeFilter::addClone(NodeID o, NodeID c) {
    objToClones[o].set(c);
}

const NodeBS &TypeFilter::getClones(NodeID o) {
    return objToClones[o];
}

void TypeFilter::setOriginalObj(NodeID c, NodeID o) {
    cloneToOriginalObj.insert({c, o});
}

NodeID TypeFilter::getOriginalObj(NodeID c) const {
    if (isClone(c)) {
        assert(cloneToOriginalObj.find(c) != cloneToOriginalObj.end()
               && "TF: original object not set for clone?");
        return cloneToOriginalObj.at(c);
    }

    return c;
}

PointsTo &TypeFilter::getFilterSet(NodeID loc) {
    return locToFilterSet[loc];
}

void TypeFilter::addGepToObj(NodeID gep, NodeID base, unsigned offset) {
    objToGeps[base].set(gep);
    const PAGNode *baseNode = ppag->getPAGNode(base);
    assert(baseNode && "TF: given bad base node?");
    const ObjPN *baseObj = SVFUtil::dyn_cast<ObjPN>(baseNode);
    assert(baseObj && "TF: non-object given for base?");
    // We can use the base or the gep mem. obj.; should be identical.
    const MemObj *baseMemObj = baseObj->getMemObj();

    objToGeps[base].set(gep);
    memObjToGeps[baseMemObj][offset].set(gep);
}

const NodeBS &TypeFilter::getGepObjsFromMemObj(const MemObj *memObj, unsigned offset) {
    return memObjToGeps[memObj][offset];
}

const NodeBS &TypeFilter::getGepObjs(NodeID base) {
    return objToGeps[base];
}

const NodeBS TypeFilter::getGepObjClones(NodeID base, const LocationSet& ls) {
    assert(dchg && "TF: DCHG not set!");
    // Set of GEP objects we will return.
    NodeBS geps;

    PAGNode *node = ppag->getPAGNode(base);
    assert(node && "TF: base object node does not exist.");
    ObjPN *baseNode = SVFUtil::dyn_cast<ObjPN>(node);
    assert(baseNode && "TF: base \"object\" node is not an object.");

    // First field? Just return the whole object; same thing.
    if (ls.getOffset() == 0) {
        // The base object is the 0 gep object.
        addGepToObj(base, base, 0);
        geps.set(base);
        return geps;
    }

    if (baseNode->getMemObj()->isFieldInsensitive()) {
        // If it's field-insensitive, the base represents everything.
        geps.set(base);
        return geps;
    }

    // TODO: caching on ls or offset will improve performance.
    const NodeBS &gepObjs = getGepObjs(base);
    for (NodeID gep : gepObjs) {
        PAGNode *node = ppag->getPAGNode(gep);
        assert(node && "TF: expected gep node doesn't exist.");
        assert((SVFUtil::isa<GepObjPN>(node) || SVFUtil::isa<FIObjPN>(node))
               && "TF: expected a GEP or FI object.");

        if (GepObjPN *gepNode = SVFUtil::dyn_cast<GepObjPN>(node)) {
            if (gepNode->getLocationSet().getOffset() == ls.getOffset()) {
                geps.set(gep);
            }
        } else {
            // Definitely a FIObj (asserted).
            geps.set(gep);
        }
    }

    if (geps.empty()) {
        // No gep node has even be created, so create one.
        NodeID newGep;
        if (isClone(base)) {
            // Don't use ppag->getGepObjNode because base and it's original object
            // have the same memory object which is the key PAG uses.
            newGep = ppag->addCloneGepObjNode(baseNode->getMemObj(), ls);
        } else {
            newGep = ppag->getGepObjNode(base, ls);
        }

        GepObjPN *gep = SVFUtil::dyn_cast<GepObjPN>(ppag->getPAGNode(newGep));
        gep->setBaseNode(base);

        addGepToObj(newGep, base, ls.getOffset());
        const DIType *baseType = getType(base);
        const DIType *newGepType;
        if (baseType->getTag() == dwarf::DW_TAG_array_type || baseType->getTag() == dwarf::DW_TAG_pointer_type) {
            if (const DICompositeType *arrayType = SVFUtil::dyn_cast<DICompositeType>(baseType)) {
                // Array access.
                newGepType = arrayType->getBaseType();
            } else if (const DIDerivedType *ptrType = SVFUtil::dyn_cast<DIDerivedType>(baseType)) {
                // Pointer access.
                newGepType = ptrType->getBaseType();
            }

            // Get the canonical type because we got the type from the DIType infrastructure directly.
            newGepType = dchg->getCanonicalType(newGepType);
        } else {
            // Must be a struct/class.
            newGepType = dchg->getFieldType(getType(base), ls.getOffset());
        }

        setType(newGep, newGepType);
        // TODO: "allocation" site does not make sense for GEP objects.
        setAllocationSite(newGep, getAllocationSite(base));

        geps.set(newGep);
    }

    return geps;
}

bool TypeFilter::init(NodeID loc, NodeID p, const DIType *tildet, bool reuse, bool gep) {
    assert(dchg && "TF: DCHG not set!");
    bool changed = false;

    PointsTo &pPt = pta->getPts(p);
    // The points-to set we will populate in the loop to fill pPt.
    PointsTo pNewPt;

    PointsTo &filterSet = getFilterSet(loc);
    for (PointsTo::iterator oI = pPt.begin(); oI != pPt.end(); ++oI) {
        NodeID o = *oI;
        const DIType *tp = getType(o);  // tp is t'

        // When an object is field-insensitive, we can't filter on any of the fields' types.
        // i.e. a pointer of the field type can safely access an object of the base/struct
        // type if that object is field-insensitive.
        bool fieldInsensitive = false;
        std::vector<const DIType *> fieldTypes;
        if (ObjPN *obj = SVFUtil::dyn_cast<ObjPN>(ppag->getPAGNode(o))) {
            fieldInsensitive = obj->getMemObj()->isFieldInsensitive();
            if (tp != nullptr && (tp->getTag() == dwarf::DW_TAG_structure_type
                                  || tp->getTag() == dwarf::DW_TAG_class_type
                                  || tp->getTag() == dwarf::DW_TAG_union_type)) {
                fieldTypes = dchg->getFieldTypes(tp);
            }
        }

        std::set<const DIType *> aggs = dchg->isAgg(tp) ? dchg->getAggs(tp) : std::set<const DIType *>();

        NodeID prop;
        bool filter = false;
        if (fieldInsensitive && std::find(fieldTypes.begin(), fieldTypes.end(), tildet) != fieldTypes.end()) {
            // Field-insensitive object but the instruction is operating on a field.
            prop = o;
        } else if (gep && aggs.find(tildet) != aggs.end()) {
            // SVF treats two consecutive GEPs as children to the same load/store.
            prop = o;
        } else if (tp == undefType) {
            // o is uninitialised.
            prop = cloneObject(o, tildet);
        } else if (isBase(tp, tildet) && tp != tildet) {
            // Downcast.
            prop = cloneObject(o, tildet);
        } else if (isBase(tildet, tp)) {
            // Upcast.
            prop = o;
        } else if (tildet != tp && reuse) {
            // Reuse.
            prop = cloneObject(o, tildet);
        } else {
            // Some spurious objects will be filtered.
            filter = true;
            prop = o;
        }

        if (tp == undefType && prop != o) {
            // If we cloned, we want to keep o in p's PTS but filter it (ignore it later).
            pNewPt.set(o);
            filterSet.set(o);
        }

        pNewPt.set(prop);

        if (filter) {
            filterSet.set(o);
        }
    }

    if (pPt != pNewPt) {
        // TODO: more performant way of doing this? Like move?
        pPt.clear();
        pPt |= pNewPt;
        changed = true;
    }

    return changed;
}

NodeID TypeFilter::cloneObject(NodeID o, const DIType *type) {
    // Always operate on the original object.
    if (isClone(o)) o = getOriginalObj(o);

    // Check if a clone of the correct type exists.
    const NodeBS &clones = getClones(o);
    for (NodeID clone : clones) {
        if (getType(clone) == type) {
            return clone;
        }
    }

    const PAGNode *obj = ppag->getPAGNode(o);
    NodeID clone = o;
    if (const GepObjPN *gepObj = SVFUtil::dyn_cast<GepObjPN>(obj)) {
        clone = ppag->addCloneGepObjNode(gepObj->getMemObj(), gepObj->getLocationSet());
        // The base needs to know about the new clone.
        addGepToObj(clone, gepObj->getBaseNode(), gepObj->getLocationSet().getOffset());
    } else if (SVFUtil::isa<FIObjPN>(obj) || SVFUtil::isa<DummyObjPN>(obj)) {
        if (const FIObjPN *fiObj = SVFUtil::dyn_cast<FIObjPN>(obj)) {
            clone = ppag->addCloneFIObjNode(fiObj->getMemObj());
        } else {
            clone = ppag->addCloneObjNode();
        }
    } else {
        assert(false && "FSTF: trying to clone unhandled object");
    }

    // Clone's metadata.
    setType(clone, type);
    setAllocationSite(clone, getAllocationSite(o));

    // Tracking object<->clone mappings.
    addClone(o, clone);
    setOriginalObj(clone, o);

    backPropagate(clone);

    return clone;
}

const MDNode *TypeFilter::getRawCTirMetadata(const Value *v) {
    assert(v != nullptr && "TF: trying to get metadata from nullptr!");

    const MDNode *mdNode = nullptr;
    if (const Instruction *inst = SVFUtil::dyn_cast<Instruction>(v)) {
        mdNode = inst->getMetadata(SVFModule::ctirMetadataName);
    } else if (const GlobalObject *go = SVFUtil::dyn_cast<GlobalObject>(v)) {
        mdNode = go->getMetadata(SVFModule::ctirMetadataName);
    }

    // Will be nullptr if metadata isn't there.
    return mdNode;
}

const MDNode *TypeFilter::getRawCTirMetadata(const SVFGNode *s) {
    if (const StmtSVFGNode *stmt = SVFUtil::dyn_cast<StmtSVFGNode>(s)) {
        const Value *v = stmt->getInst() ? stmt->getInst() : stmt->getPAGEdge()->getValue();
        if (v != nullptr) {
            return getRawCTirMetadata(v);
        }
    }

    return nullptr;
}

const DIType *TypeFilter::getTypeFromCTirMetadata(const Value *v) {
    assert(v != nullptr && "TF: trying to get type from nullptr!");

    const MDNode *mdNode = getRawCTirMetadata(v);
    if (mdNode == nullptr) {
        return nullptr;
    }

    const DIType *type = SVFUtil::dyn_cast<DIType>(mdNode);
    if (type == nullptr) {
        SVFUtil::errs() << "TF: bad ctir metadata type\n";
        return nullptr;
    }

    return dchg->getCanonicalType(type);
}

const DIType *TypeFilter::getTypeFromCTirMetadata(const SVFGNode *s) {
    if (const StmtSVFGNode *stmt = SVFUtil::dyn_cast<StmtSVFGNode>(s)) {
        const Value *v = stmt->getInst() ? stmt->getInst() : stmt->getPAGEdge()->getValue();
        if (v != nullptr) {
            return getTypeFromCTirMetadata(v);
        }
    }

    return nullptr;
}

