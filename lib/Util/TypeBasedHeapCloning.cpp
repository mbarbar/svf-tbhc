//===- TypeBasedHeapCloning.cpp -- Type filter/type-based heap cloning base ------------//

/*
 * TypeBasedHeapCloning.cpp
 *
 *  Created on: Feb 08, 2020
 *      Author: Mohamad Barbar
 */

#include "Util/CPPUtil.h"
#include "Util/TypeBasedHeapCloning.h"

const DIType *TypeBasedHeapCloning::undefType = nullptr;

const std::string TypeBasedHeapCloning::derefFnName = "deref";
const std::string TypeBasedHeapCloning::mangledDerefFnName = "_Z5derefv";

TypeBasedHeapCloning::TypeBasedHeapCloning(PointerAnalysis *pta, bool reuse) {
    this->pta = pta;
    this->reuse = reuse;
}

void TypeBasedHeapCloning::setDCHG(DCHGraph *dchg) {
    this->dchg = dchg;
}

void TypeBasedHeapCloning::setPAG(PAG *pag) {
    ppag = pag;
}

bool TypeBasedHeapCloning::isBlkObjOrConstantObj(NodeID o) const {
    if (isClone(o)) o = cloneToOriginalObj.at(o);
    return SVFUtil::isa<ObjPN>(ppag->getPAGNode(o)) && ppag->isBlkObjOrConstantObj(o);
}

bool TypeBasedHeapCloning::isBase(const DIType *a, const DIType *b) const {
    assert(dchg && "TBHC: DCHG not set!");
    return dchg->isBase(a, b, true);
}

bool TypeBasedHeapCloning::isClone(NodeID o) const {
    return cloneToOriginalObj.find(o) != cloneToOriginalObj.end();
}

void TypeBasedHeapCloning::setType(NodeID o, const DIType *t) {
    objToType.insert({o, t});
}

const DIType *TypeBasedHeapCloning::getType(NodeID o) const {
    assert(objToType.find(o) != objToType.end() && "TBHC: object has no type?");
    return objToType.at(o);
}

void TypeBasedHeapCloning::setAllocationSite(NodeID o, NodeID site) {
    objToAllocation.insert({o, site});
}

NodeID TypeBasedHeapCloning::getAllocationSite(NodeID o) const {
    assert(objToAllocation.find(o) != objToAllocation.end() && "TBHC: object has no allocation site?");
    return objToAllocation.at(o);
}

const NodeBS TypeBasedHeapCloning::getObjsWithClones(void) {
    NodeBS objs;
    for (std::pair<NodeID, NodeBS> oc : objToClones) {
        objs.set(oc.first);
    }

    return objs;
}

void TypeBasedHeapCloning::addClone(NodeID o, NodeID c) {
    objToClones[o].set(c);
}

const NodeBS &TypeBasedHeapCloning::getClones(NodeID o) {
    return objToClones[o];
}

void TypeBasedHeapCloning::setOriginalObj(NodeID c, NodeID o) {
    cloneToOriginalObj.insert({c, o});
}

NodeID TypeBasedHeapCloning::getOriginalObj(NodeID c) const {
    if (isClone(c)) {
        assert(cloneToOriginalObj.find(c) != cloneToOriginalObj.end()
               && "TBHC: original object not set for clone?");
        return cloneToOriginalObj.at(c);
    }

    return c;
}

PointsTo &TypeBasedHeapCloning::getFilterSet(NodeID loc) {
    return locToFilterSet[loc];
}

void TypeBasedHeapCloning::addGepToObj(NodeID gep, NodeID base, unsigned offset) {
    objToGeps[base].set(gep);
    const PAGNode *baseNode = ppag->getPAGNode(base);
    assert(baseNode && "TBHC: given bad base node?");
    const ObjPN *baseObj = SVFUtil::dyn_cast<ObjPN>(baseNode);
    assert(baseObj && "TBHC: non-object given for base?");
    // We can use the base or the gep mem. obj.; should be identical.
    const MemObj *baseMemObj = baseObj->getMemObj();

    objToGeps[base].set(gep);
    memObjToGeps[baseMemObj][offset].set(gep);
}

const NodeBS &TypeBasedHeapCloning::getGepObjsFromMemObj(const MemObj *memObj, unsigned offset) {
    return memObjToGeps[memObj][offset];
}

const NodeBS &TypeBasedHeapCloning::getGepObjs(NodeID base) {
    return objToGeps[base];
}

const NodeBS TypeBasedHeapCloning::getGepObjClones(NodeID base, const LocationSet& ls) {
    assert(dchg && "TBHC: DCHG not set!");
    // Set of GEP objects we will return.
    NodeBS geps;

    PAGNode *node = ppag->getPAGNode(base);
    assert(node && "TBHC: base object node does not exist.");
    ObjPN *baseNode = SVFUtil::dyn_cast<ObjPN>(node);
    assert(baseNode && "TBHC: base \"object\" node is not an object.");

    const DIType *baseType = getType(base);

    // First field? Just return the whole object; same thing.
    // For arrays, we want things to work as normal because an array *object* is more
    // like a pointer than a struct object.
    if (ls.getOffset() == 0 && baseType->getTag() != dwarf::DW_TAG_array_type) {
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

    // Caching on ls or offset would improve performance but it seems minimal.
    const NodeBS &gepObjs = getGepObjs(base);
    for (NodeID gep : gepObjs) {
        PAGNode *node = ppag->getPAGNode(gep);
        assert(node && "TBHC: expected gep node doesn't exist.");
        assert((SVFUtil::isa<GepObjPN>(node) || SVFUtil::isa<FIObjPN>(node))
               && "TBHC: expected a GEP or FI object.");

        if (GepObjPN *gepNode = SVFUtil::dyn_cast<GepObjPN>(node)) {
            if (gepNode->getLocationSet().getOffset() == ls.getOffset()) {
                geps.set(gep);
            }
        } else {
            // Definitely a FIObj (asserted), but we don't want to add it if
            // the object is field-sensitive because in that case it actually
            // represents the 0th field, not the whole object.
            if (baseNode->getMemObj()->isFieldInsensitive()) {
                geps.set(gep);
            }
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
        setAllocationSite(newGep, 0);

        geps.set(newGep);
    }

    return geps;
}

bool TypeBasedHeapCloning::init(NodeID loc, NodeID p, const DIType *tildet, bool reusePossible, bool gep) {
    assert(dchg && "TBHC: DCHG not set!");
    // Ignore reusePossible if reuse is disabled.
    reusePossible = reusePossible && reuse;
    bool changed = false;

    PointsTo &pPt = pta->getPts(p);
    // The points-to set we will populate in the loop to fill pPt.
    PointsTo pNewPt;

    PointsTo &filterSet = getFilterSet(loc);
    for (NodeID o : pPt) {
        PAGNode *obj = ppag->getPAGNode(o);
        assert(obj && "TBHC: pointee object does not exist in PAG?");
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
        // We might need this if it's an array.
        const DICompositeType *arrType = tp != undefType && tp->getTag() == dwarf::DW_TAG_array_type
                                         ? SVFUtil::dyn_cast<DICompositeType>(tp) : nullptr;
        if (fieldInsensitive && std::find(fieldTypes.begin(), fieldTypes.end(), tildet) != fieldTypes.end()) {
            // Field-insensitive object but the instruction is operating on a field.
            prop = o;
            ++numTBWU;
        } else if (gep && arrType != nullptr
                   && dchg->getCanonicalType(arrType->getBaseType()) == tildet) {
            // Special case for arrays of structs or other aggregates.
            // SVF will transform 
            //    `1: s = get struct element X from array a; 2: f = get field of struct Y from s;`
            // to `1: s = get struct element X from array a; 2: f = get field of struct Y from a;`
            // so we want the second instruction to be operating on an object of type
            // 'Struct S', not 'Array of S'.
            prop = cloneObject(o, dchg->getCanonicalType(arrType->getBaseType()));
            ++numArray;
            if (!pta->isHeapMemObj(o) && !SVFUtil::isa<DummyObjPN>(obj)) ++numSGArray;
        } else if (gep && aggs.find(tildet) != aggs.end()) {
            // SVF treats two consecutive GEPs as children to the same load/store.
            prop = o;
            ++numTBWU;
        } else if (tp == undefType) {
            // o is uninitialised.
            // GEP objects should never be uninitialised; type assigned at creation.
            assert(!isGep(obj) && "TBHC: GEP object is untyped!");
            prop = cloneObject(o, tildet);
            ++numInit;
            if (!pta->isHeapMemObj(o) && !SVFUtil::isa<DummyObjPN>(obj)) ++numSGInit;
        } else if (isBase(tp, tildet) && tp != tildet
                   && ((!reusePossible && !isGep(obj)) || reusePossible)) {
            // Downcast.
            // !reuse && !gep because field types are static.
            prop = cloneObject(o, tildet);
            ++numTBSSU;
            if (!pta->isHeapMemObj(o) && !SVFUtil::isa<DummyObjPN>(obj)) ++numSGTBSSU;
        } else if (isBase(tildet, tp)) {
            // Upcast.
            prop = o;
            ++numTBWU;
            if (!pta->isHeapMemObj(o) && !SVFUtil::isa<DummyObjPN>(obj)) ++numSGTBWU;
        } else if (tildet != tp && reusePossible) {
            // Reuse.
            prop = cloneObject(o, tildet);
            ++numReuse;
            if (!pta->isHeapMemObj(o) && !SVFUtil::isa<DummyObjPN>(obj)) ++numSGReuse;
        } else {
            // Some spurious objects will be filtered.
            filter = true;
            prop = o;
            ++numTBSU;
            if (!pta->isHeapMemObj(o) && !SVFUtil::isa<DummyObjPN>(obj)) ++numSGTBSU;
        }

        if (prop != o) {
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
        // Seems fast enough to perform in the naive way.
        pPt.clear();
        pPt |= pNewPt;
        changed = true;
    }

    return changed;
}

NodeID TypeBasedHeapCloning::cloneObject(NodeID o, const DIType *type) {
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
        assert(false && "FSTBHC: trying to clone unhandled object");
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

const MDNode *TypeBasedHeapCloning::getRawCTirMetadata(const Value *v) {
    assert(v != nullptr && "TBHC: trying to get metadata from nullptr!");

    const MDNode *mdNode = nullptr;
    if (const Instruction *inst = SVFUtil::dyn_cast<Instruction>(v)) {
        mdNode = inst->getMetadata(cppUtil::ctir::derefMDName);
    } else if (const GlobalObject *go = SVFUtil::dyn_cast<GlobalObject>(v)) {
        mdNode = go->getMetadata(cppUtil::ctir::derefMDName);
    }

    // Will be nullptr if metadata isn't there.
    return mdNode;
}

const DIType *TypeBasedHeapCloning::getTypeFromCTirMetadata(const Value *v) {
    assert(v != nullptr && "TBHC: trying to get type from nullptr!");

    const MDNode *mdNode = getRawCTirMetadata(v);
    if (mdNode == nullptr) {
        return nullptr;
    }

    const DIType *type = SVFUtil::dyn_cast<DIType>(mdNode);
    if (type == nullptr) {
        SVFUtil::errs() << "TBHC: bad ctir metadata type\n";
        return nullptr;
    }

    return dchg->getCanonicalType(type);
}

bool TypeBasedHeapCloning::isGep(const PAGNode *n) const {
    assert(n != nullptr && "TBHC: testing if null is a GEP object!");
    return SVFUtil::isa<GepObjPN>(n);
}

/// Returns true if the function name matches MAYALIAS, NOALIAS, etc.
static bool isAliasTestFunction(std::string name) {
    return    name == PointerAnalysis::aliasTestMayAlias
           || name == PointerAnalysis::aliasTestMayAliasMangled
           || name == PointerAnalysis::aliasTestNoAlias
           || name == PointerAnalysis::aliasTestNoAliasMangled
           || name == PointerAnalysis::aliasTestPartialAlias
           || name == PointerAnalysis::aliasTestPartialAliasMangled
           || name == PointerAnalysis::aliasTestMustAlias
           || name == PointerAnalysis::aliasTestMustAliasMangled
           || name == PointerAnalysis::aliasTestFailMayAlias
           || name == PointerAnalysis::aliasTestFailMayAliasMangled
           || name == PointerAnalysis::aliasTestFailNoAlias
           || name == PointerAnalysis::aliasTestFailNoAliasMangled;
}

void TypeBasedHeapCloning::validateTBHCTests(SVFModule &svfMod) {
    SVFUtil::outs() << "!!!!!! VALIDATING TBHC TESTS !!!!!!\n";
    for (u32_t i = 0; i < svfMod.getModuleNum(); ++i) {
        Module *module = svfMod.getModule(i);
        const PAG::CallSiteSet &callSites = ppag->getCallSiteSet();
        for (const CallSite &cs : callSites) {
            const Function *fn = cs.getCalledFunction();
            if (fn == nullptr || !isAliasTestFunction(fn->getName())) {
                continue;
            }

            // We have a test call,
            // We want the store which stores to the pointer in question (i.e. operand of the
            // store is the pointer, and the store itself is the dereference).
            const StoreInst *ps = nullptr, *qs = nullptr;
            // Check: currInst is a deref call, so p/q is prevInst.

            // Find p.
            const Instruction *prevInst = nullptr;
            const Instruction *currInst = cs.getInstruction();
            do {
                if (const CallInst *ci = SVFUtil::dyn_cast<CallInst>(currInst)) {
                    std::string calledFnName = ci->getCalledFunction()->getName().str();
                    if (calledFnName == derefFnName || calledFnName == mangledDerefFnName) {
                        const StoreInst *si = SVFUtil::dyn_cast<StoreInst>(prevInst);
                        assert(si && "TBHC: validation macro not producing stores?");
                        ps = si;
                        break;
                    }
                }

                prevInst = currInst;
                currInst = currInst->getNextNonDebugInstruction();
            } while (currInst != nullptr);

            // Repeat for q.
            while (currInst != nullptr) {
                // while loop, not do-while, because we need to the next instruction (current
                // instruction is the first deref()).
                prevInst = currInst;
                currInst = currInst->getNextNonDebugInstruction();

                if (const CallInst *ci = SVFUtil::dyn_cast<CallInst>(currInst)) {
                    std::string calledFnName = ci->getCalledFunction()->getName().str();
                    if (calledFnName == derefFnName || calledFnName == mangledDerefFnName) {
                        const StoreInst *si = SVFUtil::dyn_cast<StoreInst>(prevInst);
                        assert(si && "TBHC: validation macro not producing stores?");
                        qs = si;
                        break;
                    }
                }
            }

            assert(ps != nullptr && qs != nullptr && "TBHC: malformed alias test?");
            NodeID p = ppag->getValueNode(ps->getPointerOperand()),
                   q = ppag->getValueNode(qs->getPointerOperand());
            const DIType *pt = getTypeFromCTirMetadata(ps), *qt = getTypeFromCTirMetadata(qs);

            // Now filter both points-to sets according to the type of the value.
            const PointsTo &pPts = pta->getPts(p), &qPts = pta->getPts(q);
            PointsTo pPtsFiltered, qPtsFiltered;
            for (NodeID o : pPts) {
                if (getType(o) != undefType && isBase(pt, getType(o))) {
                    pPtsFiltered.set(o);
                }
            }

            for (NodeID o : qPts) {
                if (getType(o) != undefType && isBase(qt, getType(o))) {
                    qPtsFiltered.set(o);
                }
            }

            BVDataPTAImpl *bvpta = SVFUtil::dyn_cast<BVDataPTAImpl>(pta);
            assert(bvpta && "TBHC: need a BVDataPTAImpl for TBHC alias testing.");
            AliasResult res = bvpta->alias(pPtsFiltered, qPtsFiltered);

            bool passed = false;
            if (fn->getName() == PointerAnalysis::aliasTestMayAlias
                || fn->getName() == PointerAnalysis::aliasTestMayAliasMangled) {
                passed = res == llvm::MayAlias || res == llvm::MustAlias;
            } else if (fn->getName() == PointerAnalysis::aliasTestNoAlias
                       || fn->getName() == PointerAnalysis::aliasTestNoAliasMangled) {
                passed = res == llvm::NoAlias;
            } else if (fn->getName() == PointerAnalysis::aliasTestMustAlias
                       || fn->getName() == PointerAnalysis::aliasTestMustAliasMangled) {
                passed = res == llvm::MustAlias || res == llvm::MayAlias;
            } else if (fn->getName() == PointerAnalysis::aliasTestPartialAlias
                       || fn->getName() == PointerAnalysis::aliasTestPartialAliasMangled) {
                passed = res == llvm::MayAlias || res == llvm::PartialAlias;
            } else if (fn->getName() == PointerAnalysis::aliasTestFailMayAlias
                       || fn->getName() == PointerAnalysis::aliasTestFailMayAliasMangled) {
                passed = res != llvm::MayAlias && res != llvm::MustAlias && res != llvm::PartialAlias;
            } else if (fn->getName() == PointerAnalysis::aliasTestFailNoAlias
                       || fn->getName() == PointerAnalysis::aliasTestFailNoAliasMangled) {
                passed = res != llvm::NoAlias;
            }

            SVFUtil::outs() << "[" << pta->PTAName() << "] Checking " << fn->getName() << "\n";
            raw_ostream &msgStream = passed ? SVFUtil::outs() : SVFUtil::errs();
            msgStream << (passed ? SVFUtil::sucMsg("\t SUCCESS") : SVFUtil::errMsg("\t FAILURE"))
                      << " : " << fn->getName()
                      << " check <id:" << p << ", id:" << q << "> "
                      << "at (" << SVFUtil::getSourceLoc(cs.getInstruction()) << ")\n";

            if (pPtsFiltered.empty()) {
                msgStream << SVFUtil::wrnMsg("\t WARNING")
                          << " : pts(" << p << ") is empty\n";
            }

            if (qPtsFiltered.empty()) {
                msgStream << SVFUtil::wrnMsg("\t WARNING")
                          << " : pts(" << q << ") is empty\n";
            }

            if (fn->getName() == PointerAnalysis::aliasTestMustAlias
                || fn->getName() == PointerAnalysis::aliasTestMustAliasMangled) {
                msgStream << SVFUtil::wrnMsg("\t WARNING")
                          << " : MUSTALIAS tests are actually MAYALIAS tests\n";
            }

            if (fn->getName() == PointerAnalysis::aliasTestPartialAlias
                || fn->getName() == PointerAnalysis::aliasTestPartialAliasMangled) {
                msgStream << SVFUtil::wrnMsg("\t WARNING")
                          << " : PARTIALALIAS tests are actually MAYALIAS tests\n";
            }

            msgStream.flush();
        }
    }
}

void TypeBasedHeapCloning::dumpStats(void) {
    std::string indent = "";
    SVFUtil::outs() << "@@@@@@@@@ TBHC STATISTICS @@@@@@@@@\n";
    indent = "  ";

    // Print clones with their types.
    SVFUtil::outs() << indent << "=== Original objects to clones ===\n";
    indent = "    ";
    unsigned total = 0;
    const NodeBS objs = getObjsWithClones();
    for (NodeID o : objs) {
        const NodeBS &clones = getClones(o);
        if (clones.count() == 0) continue;

        total += clones.count();
        SVFUtil::outs() << indent
                        << "  " << o << " : "
                        << "(" << clones.count() << ")"
                        << "[ ";
        bool first = true;
        for (NodeID c : clones) {
            if (!first) {
                SVFUtil::outs() << ", ";
            }

            SVFUtil::outs() << c
                            << "{"
                            << dchg->diTypeToStr(getType(c))
                            << "}";
            first = false;
        }

        SVFUtil::outs() << " ]\n";
    }

    indent = "  ";
    SVFUtil::outs() << indent << "Total: " << total << "\n";
    SVFUtil::outs() << indent << "==================================\n";

    SVFUtil::outs() << indent << "INITIALISE : " << numInit  << "\n";
    SVFUtil::outs() << indent << "TBWU       : " << numTBWU  << "\n";
    SVFUtil::outs() << indent << "TBSSU      : " << numTBSSU << "\n";
    SVFUtil::outs() << indent << "TBSU       : " << numTBSU  << "\n";
    SVFUtil::outs() << indent << "REUSE      : " << numReuse << "\n";
    SVFUtil::outs() << indent << "ARRAY CASE : " << numArray << "\n";

    SVFUtil::outs() << "\n";
    SVFUtil::outs() << indent << "STACK/GLOBAL OBJECTS\n";
    indent = "    ";
    SVFUtil::outs() << indent << "INITIALISE : " << numSGInit  << "\n";
    SVFUtil::outs() << indent << "TBWU       : " << numSGTBWU  << "\n";
    SVFUtil::outs() << indent << "TBSSU      : " << numSGTBSSU << "\n";
    SVFUtil::outs() << indent << "TBSU       : " << numSGTBSU  << "\n";
    SVFUtil::outs() << indent << "REUSE      : " << numSGReuse << "\n";
    SVFUtil::outs() << indent << "ARRAY CASE : " << numSGArray << "\n";

    SVFUtil::outs() << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n";
}
