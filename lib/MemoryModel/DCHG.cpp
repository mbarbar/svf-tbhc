//===----- DCHG.cpp  CHG using DWARF debug info ------------------------//
//
/*
 * DCHG.cpp
 *
 *  Created on: Aug 24, 2019
 *      Author: Mohamad Barbar
 */

#include <sstream>

#include "MemoryModel/DCHG.h"
#include "Util/CPPUtil.h"

#include "llvm/IR/DebugInfo.h"

static llvm::cl::opt<bool> printDCHG("print-dchg", llvm::cl::init(false), llvm::cl::desc("print the DCHG if debug information is available"));

// TODO: not used for ctir.
const std::string DCHGraph::ctirInternalUntypedName = "__ctir_internal_untyped";

void DCHGraph::handleDIBasicType(const llvm::DIBasicType *basicType) {
    getOrCreateNode(basicType);
}

void DCHGraph::handleDICompositeType(const llvm::DICompositeType *compositeType) {
    switch (compositeType->getTag()) {
    case llvm::dwarf::DW_TAG_array_type:
        if (extended) getOrCreateNode(compositeType);
        gatherAggs(compositeType);
        break;
    case llvm::dwarf::DW_TAG_class_type:
    case llvm::dwarf::DW_TAG_structure_type:
        getOrCreateNode(compositeType);
        // If we're extending, we need to add the first-field relation.
        if (extended) {
            llvm::DINodeArray fields = compositeType->getElements();
            if (!fields.empty()) {
                // fields[0] gives a type which is DW_TAG_member, we want the member's type (getBaseType).
                // It can also give a Subprogram type if the class just had non-virtual functions.
                // We can happily ignore that.
                llvm::DIDerivedType *firstMember = SVFUtil::dyn_cast<llvm::DIDerivedType>(fields[0]);
                if (firstMember != NULL) {
                    addEdge(compositeType, firstMember->getBaseType(), DCHEdge::FIRST_FIELD);
                }
            }
        }

        flatten(compositeType);
        gatherAggs(compositeType);

        break;
    case llvm::dwarf::DW_TAG_union_type:
        getOrCreateNode(compositeType);
        // All fields are first fields.
        if (extended) {
            llvm::DINodeArray fields = compositeType->getElements();
            for (llvm::DINode *field : fields) {
                // fields[0] gives a type which is DW_TAG_member, we want the member's type (getBaseType).
                llvm::DIDerivedType *firstMember = SVFUtil::dyn_cast<llvm::DIDerivedType>(field);
                // Is this check necessary? TODO
                if (firstMember != NULL) {
                    addEdge(compositeType, firstMember->getBaseType(), DCHEdge::FIRST_FIELD);
                }
            }
        }

        flatten(compositeType);
        gatherAggs(compositeType);

        break;
    case llvm::dwarf::DW_TAG_enumeration_type:
        // TODO: maybe just drop these to the base type?
        getOrCreateNode(compositeType);
        break;
    default:
        assert(false && "DCHGraph::buildCHG: unexpected CompositeType tag.");
    }
}

void DCHGraph::handleDIDerivedType(const llvm::DIDerivedType *derivedType) {
    switch (derivedType->getTag()) {
    case llvm::dwarf::DW_TAG_inheritance: {
        assert(SVFUtil::isa<llvm::DIType>(derivedType->getScope()) && "inheriting from non-type?");
        DCHEdge *edge = addEdge(SVFUtil::dyn_cast<llvm::DIType>(derivedType->getScope()),
                                derivedType->getBaseType(), DCHEdge::INHERITANCE);
        // If the offset does not exist (for primary base), getOffset should return 0.
        edge->setOffset(derivedType->getOffsetInBits());
        break;
    }
    case llvm::dwarf::DW_TAG_member:
        // TODO: don't care it seems.
        break;
    case llvm::dwarf::DW_TAG_typedef:
        handleTypedef(derivedType);
        break;
    case llvm::dwarf::DW_TAG_pointer_type:
        if (extended) getOrCreateNode(derivedType);
        break;
    case llvm::dwarf::DW_TAG_ptr_to_member_type:
        if (extended) getOrCreateNode(derivedType);
        break;
    case llvm::dwarf::DW_TAG_reference_type:
        if (extended) getOrCreateNode(derivedType);
        break;
    case llvm::dwarf::DW_TAG_rvalue_reference_type:
        // TODO: are these just pointers?
        break;
    case llvm::dwarf::DW_TAG_const_type:
        // TODO: need flags for qualifiers.
        break;
    case llvm::dwarf::DW_TAG_atomic_type:
        // TODO: need flags for qualifiers.
        break;
    case llvm::dwarf::DW_TAG_volatile_type:
        // TODO: need flags for qualifiers.
        break;
    case llvm::dwarf::DW_TAG_restrict_type:
        // TODO: need flags for qualifiers.
        break;
    case llvm::dwarf::DW_TAG_friend:
        // TODO: unsure.
        break;
    default:
        assert(false && "DCHGraph::buildCHG: unexpected DerivedType tag.");
    }
}

void DCHGraph::handleDISubroutineType(const llvm::DISubroutineType *subroutineType) {
    getOrCreateNode(subroutineType);
}

void DCHGraph::handleTypedef(const llvm::DIType *typedefType) {
    assert(typedefType && typedefType->getTag() == llvm::dwarf::DW_TAG_typedef);

    // Need to gather them in a set first because we don't know the base type till
    // we get to the bottom of the (potentially many) typedefs.
    std::vector<const llvm::DIDerivedType *> typedefs;
    // Check for NULL because you can typedef void.
    while (typedefType != NULL && typedefType->getTag() == llvm::dwarf::DW_TAG_typedef) {
        const llvm::DIDerivedType *typedefDerivedType = SVFUtil::dyn_cast<llvm::DIDerivedType>(typedefType);
        // The typedef itself.
        typedefs.push_back(typedefDerivedType);

        // Next in the typedef line.
        typedefType = typedefDerivedType->getBaseType();
    }

    const llvm::DIType *baseType = typedefType;
    DCHNode *baseTypeNode = getOrCreateNode(baseType);

    for (std::vector<const llvm::DIDerivedType *>::iterator typedefI = typedefs.begin(); typedefI != typedefs.end(); ++typedefI) {
        // Base type needs to hold its typedefs.
        baseTypeNode->addTypedef(*typedefI);
    }
}

void DCHGraph::buildVTables(const Module &module) {
    for (Module::const_global_iterator gvI = module.global_begin(); gvI != module.global_end(); ++gvI) {
        // Though this will return more than GlobalVariables, we only care about GlobalVariables (for the vtbls).
        const GlobalVariable *gv = SVFUtil::dyn_cast<const GlobalVariable>(&(*gvI));
        if (gv == nullptr) continue;
        if (gv->hasMetadata("ctirvt") && gv->getNumOperands() > 0) {
            llvm::DIType *type = llvm::dyn_cast<llvm::DIType>(gv->getMetadata("ctirvt"));
            assert(type && "Bad metadata for ctirvt");
            DCHNode *node = getOrCreateNode(type);
            node->setVTable(gv);
            vtblToTypeMap[gv] = getCanonicalType(type);

            const ConstantStruct *vtbls = SVFUtil::dyn_cast<ConstantStruct>(gv->getOperand(0));
            assert(vtbls && "unexpected vtable type");
            for (unsigned nthVtbl = 0; nthVtbl < vtbls->getNumOperands(); ++nthVtbl) {
                const ConstantArray *vtbl = SVFUtil::dyn_cast<ConstantArray>(vtbls->getOperand(nthVtbl));
                assert(vtbl && "Element of vtbl struct not an array");

                std::vector<const Function *> &vfns = node->getVfnVector(nthVtbl);

                // Iterating over the vtbl, we will run into:
                // 1. i8* null         (don't care for now).
                // 2. i8* inttoptr ... (don't care for now).
                // 3. i8* bitcast  ... (we only care when a function pointer is being bitcasted).
                for (unsigned cN = 0; cN < vtbl->getNumOperands(); ++cN) {
                    Constant *c = vtbl->getOperand(cN);
                    if (SVFUtil::isa<ConstantPointerNull>(c)) {
                        // Don't care for now.
                        continue;
                    }

                    ConstantExpr *ce = SVFUtil::dyn_cast<ConstantExpr>(c);
                    assert(ce && "non-ConstantExpr, non-ConstantPointerNull in vtable?");
                    if (ce->getOpcode() != Instruction::BitCast) {
                        continue;
                    }

                    // Could be a GlobalAlias which we don't care about, or a virtual/thunk function.
                    const Function *vfn = SVFUtil::dyn_cast<Function>(ce->getOperand(0));
                    if (vfn == nullptr) {
                        continue;
                    }

                    vfns.push_back(vfn);
                }
            }
        }
    }
}

std::set<const DCHNode *> &DCHGraph::cha(const llvm::DIType *type, bool firstField) {
    type = getCanonicalType(type);
    std::map<const llvm::DIType *, std::set<const DCHNode *>> &cacheMap =
        firstField ? chaFFMap : chaMap;

    // Check if we've already computed.
    if (cacheMap.find(type) != cacheMap.end()) {
        return cacheMap[type];
    }

    std::set<const DCHNode *> children;
    const DCHNode *node = getOrCreateNode(type);
    // Consider oneself a child, otherwise the recursion will just come up with nothing.
    children.insert(node);
    for (DCHEdge::DCHEdgeSetTy::const_iterator edgeI = node->getInEdges().begin(); edgeI != node->getInEdges().end(); ++edgeI) {
        DCHEdge *edge = *edgeI;
        // Don't care about anything but inheritance and first-field edges.
        if (edge->getEdgeKind() == DCHEdge::FIRST_FIELD) {
            if (!firstField) continue;
        } else if (edge->getEdgeKind() != DCHEdge::INHERITANCE) {
            continue;
        }

        std::set<const DCHNode *> cchildren = cha(edge->getSrcNode()->getType(), firstField);
        // Children's children are my children.
        children.insert(cchildren.begin(), cchildren.end());
    }

    // Cache results.
    cacheMap[type] = children;
    // Return the permanent object; we're returning a reference.
    return cacheMap[type];
}

void DCHGraph::flatten(const DICompositeType *type) {
    if (fieldTypes.find(type) != fieldTypes.end()) {
        // Already done (necessary because of the recursion).
        return;
    }

    assert(type != nullptr
           && (type->getTag() == dwarf::DW_TAG_class_type
               || type->getTag() == dwarf::DW_TAG_structure_type
               || type->getTag() == dwarf::DW_TAG_union_type)
           && "DCHG::flatten: expected a class/struct");

    std::vector<const DIType *> &flattenedComposite = fieldTypes[type];
    llvm::DINodeArray fields = type->getElements();
    for (unsigned i = 0; i < fields.size(); ++i) {
        if (const DISubprogram *sp = SVFUtil::dyn_cast<DISubprogram>(fields[i])) {
            // sp->getType should be a SubroutineType. TODO: assert it?
            flattenedComposite.push_back(getCanonicalType(sp->getType()));
        } else if (const DIDerivedType *mt = SVFUtil::dyn_cast<DIDerivedType>(fields[i])) {
            assert((mt->getTag() == dwarf::DW_TAG_member || mt->getTag() == dwarf::DW_TAG_inheritance)
                   && "DCHG: expected member");
            // Either we have a class, struct, union, array, or something not in need of flattening.
            const DIType *fieldType = mt->getBaseType();
            fieldType = stripQualifiers(fieldType);
            if (fieldType->getTag() == dwarf::DW_TAG_structure_type
                || fieldType->getTag() == dwarf::DW_TAG_class_type
                || fieldType->getTag() == dwarf::DW_TAG_union_type) {
                flatten(SVFUtil::dyn_cast<DICompositeType>(fieldType));
                flattenedComposite.insert(flattenedComposite.end(),
                                          // Must be canonical because that is what we inserted.
                                          fieldTypes.at(fieldType).begin(),
                                          fieldTypes.at(fieldType).end());
            } else if (fieldType->getTag() == dwarf::DW_TAG_array_type) {
                const DICompositeType *arrayType = SVFUtil::dyn_cast<DICompositeType>(fieldType);
                const DIType *baseType = arrayType->getBaseType();
                baseType = stripQualifiers(baseType);
                if (const DICompositeType *cbt = SVFUtil::dyn_cast<DICompositeType>(baseType)) {
                    flatten(cbt);
                    flattenedComposite.insert(flattenedComposite.end(),
                                              // Must be canonical because that is what we inserted.
                                              fieldTypes.at(cbt).begin(),
                                              fieldTypes.at(cbt).end());
                } else {
                    flattenedComposite.push_back(getCanonicalType(baseType));
                }
            } else {
                flattenedComposite.push_back(getCanonicalType(fieldType));
            }
        } else {
            assert(false && "DCHG: unexpected field type");
        }
    }
}

bool DCHGraph::isAgg(const DIType *t) {
    if (t == nullptr) return false;
    return    t->getTag() == dwarf::DW_TAG_array_type
           || t->getTag() == dwarf::DW_TAG_structure_type
           || t->getTag() == dwarf::DW_TAG_class_type
           || t->getTag() == dwarf::DW_TAG_union_type;
}

void DCHGraph::gatherAggs(const DICompositeType *type) {
    // A bit similar to flatten. TODO: probably merge the two?
    if (containingAggs.find(getCanonicalType(type)) != containingAggs.end()) {
        return;
    }

    std::set<const DIType *> &aggs = containingAggs[getCanonicalType(type)];
    if (type->getTag() == dwarf::DW_TAG_array_type) {
        const DIType *bt = type->getBaseType();
        if (bt->getTag() == dwarf::DW_TAG_typedef) {
            bt = stripQualifiers(bt);
        }

        if (isAgg(bt)) {
            const DICompositeType *cbt = SVFUtil::dyn_cast<DICompositeType>(bt);
            aggs.insert(getCanonicalType(cbt));
            gatherAggs(cbt);
            // These must be canonical already because of aggs.insert above/below.
            aggs.insert(containingAggs[cbt].begin(),
                        containingAggs[cbt].end());
        }
    } else {
        llvm::DINodeArray fields = type->getElements();
        for (unsigned i = 0; i < fields.size(); ++i) {
            // Unwrap the member.
            const DIDerivedType *mt = SVFUtil::dyn_cast<DIDerivedType>(fields[i]);
            const DIType *ft = mt->getBaseType();
            if (ft->getTag() == dwarf::DW_TAG_typedef) {
                ft = stripQualifiers(ft);
            }

            if (isAgg(ft)) {
                const DICompositeType *cft = SVFUtil::dyn_cast<DICompositeType>(ft);
                aggs.insert(getCanonicalType(cft));
                gatherAggs(cft);
                // These must be canonical already because of aggs.insert above.
                aggs.insert(containingAggs[cft].begin(),
                            containingAggs[cft].end());
            }
        }
    }
}

DCHNode *DCHGraph::getOrCreateNode(const llvm::DIType *type) {
    type = getCanonicalType(type);

    // Check, does the node for type exist?
    if (diTypeToNodeMap[type] != NULL) {
        return diTypeToNodeMap[type];
    }

    DCHNode *node = new DCHNode(type, numTypes++);
    addGNode(node->getId(), node);
    diTypeToNodeMap[type] = node;
    // TODO: name map, necessary?

    // TODO: handle templates.

    return node;
}

DCHEdge *DCHGraph::addEdge(const llvm::DIType *t1, const llvm::DIType *t2, DCHEdge::GEdgeKind et) {
    DCHNode *src = getOrCreateNode(t1);
    DCHNode *dst = getOrCreateNode(t2);

    DCHEdge *edge = hasEdge(t1, t2, et);
    if (edge == NULL) {
        // Create a new edge.
        edge = new DCHEdge(src, dst, et);
        src->addOutgoingEdge(edge);
        dst->addIncomingEdge(edge);
    }

    return edge;
}

DCHEdge *DCHGraph::hasEdge(const llvm::DIType *t1, const llvm::DIType *t2, DCHEdge::GEdgeKind et) {
    DCHNode *src = getOrCreateNode(t1);
    DCHNode *dst = getOrCreateNode(t2);

    for (DCHEdge::DCHEdgeSetTy::const_iterator edgeI = src->getOutEdges().begin(); edgeI != src->getOutEdges().end(); ++edgeI) {
        DCHNode *node = (*edgeI)->getDstNode();
        DCHEdge::GEdgeKind edgeType = (*edgeI)->getEdgeKind();
        if (node == dst && edgeType == et) {
            assert(SVFUtil::isa<DCHEdge>(*edgeI) && "Non-DCHEdge in DCHNode edge set.");
            return *edgeI;
        }
    }

    return NULL;
}

void DCHGraph::buildCHG(bool extend) {
    extended = extend;
    llvm::DebugInfoFinder finder;
    for (u32_t i = 0; i < svfModule.getModuleNum(); ++i) {
       finder.processModule(*(svfModule.getModule(i)));
    }

    // Create the void node regardless of whether it appears.
    getOrCreateNode(nullptr);
    // Find any char type.
    DIType *charType = nullptr;
    // We want void at the top, char as a child, and everything is a child of char:
    //     void
    //      |
    //     char
    //    / | \
    //   x  y  z


    for (llvm::DebugInfoFinder::type_iterator diTypeI = finder.types().begin(); diTypeI != finder.types().end(); ++diTypeI) {
        llvm::DIType *type = *diTypeI;
        if (llvm::DIBasicType *basicType = SVFUtil::dyn_cast<llvm::DIBasicType>(type)) {
            if (basicType->getEncoding() == dwarf::DW_ATE_unsigned_char
                || basicType->getEncoding() == dwarf::DW_ATE_signed_char) {
                charType = type;
            }

            handleDIBasicType(basicType);
        } else if (llvm::DICompositeType *compositeType = SVFUtil::dyn_cast<llvm::DICompositeType>(type)) {
            handleDICompositeType(compositeType);
        } else if (llvm::DIDerivedType *derivedType = SVFUtil::dyn_cast<llvm::DIDerivedType>(type)) {
            handleDIDerivedType(derivedType);
        } else if (llvm::DISubroutineType *subroutineType = SVFUtil::dyn_cast<llvm::DISubroutineType>(type)) {
            handleDISubroutineType(subroutineType);
        } else {
            assert(false && "DCHGraph::buildCHG: unexpected DIType.");
        }
    }

    for (u32_t i = 0; i < svfModule.getModuleNum(); ++i) {
        buildVTables(*(svfModule.getModule(i)));
    }

    // Build the void/char/everything else relation.
    // TODO: for cleanliness these should probably be some special edge, not FF/inheritance.
    if (extended && charType != nullptr) {
        // void <-- char
        addEdge(charType, nullptr, DCHEdge::INHERITANCE);
        // char <-- x, char <-- y, ...
        for (iterator nodeI = begin(); nodeI != end(); ++nodeI) {
            // Everything without a parent gets char as a parent.
            if (nodeI->second->getType() != nullptr
                && nodeI->second->getOutEdges().size() == 0) {
                addEdge(nodeI->second->getType(), charType, DCHEdge::INHERITANCE);
            }
        }
    }

    // Build the constructor to type map.
    for (llvm::DebugInfoFinder::subprogram_iterator spI = finder.subprograms().begin(); spI != finder.subprograms().end(); ++spI) {
        const DISubprogram *sp = *spI;
        // Use linkage type to ensure we have the "distinct" item.
        const Function *fn = svfModule.getFunction(sp->getLinkageName());
        if (fn != nullptr && cppUtil::isConstructor(fn)) {
            // We get the type which is a DISubroutineType.
            const DISubroutineType *st = SVFUtil::dyn_cast<DISubroutineType>(sp->getType());
            assert(st && "DCHG: subprogram type is not a subroutine?");
            // From the subroutine we get the list of types.
            const llvm::DITypeRefArray types = st->getTypeArray();
            // The first type is the return type (null, here), and the second type is
            // for the first argument (`this` since this is a constructor).
            assert(types.size() >= 2 && "DCHG: constructor does not have 2+ types?");
            // The first argument must have a pointer type (C *this).
            const DIDerivedType *pt = SVFUtil::dyn_cast<DIDerivedType>(types[1]);
            assert(pt != nullptr && pt->getTag() == dwarf::DW_TAG_pointer_type
                   && "DCHG: type of this argument is not pointer type?");
            // Base type is the type corresponding to the constructor.
            constructorToType[fn] = getCanonicalType(pt->getBaseType());
        }
    }

    if (printDCHG) {
        print();
    }
}

const VFunSet &DCHGraph::getCSVFsBasedonCHA(CallSite cs) {
    if (csCHAMap.find(cs) != csCHAMap.end()) {
        return csCHAMap.at(cs);
    }

    VFunSet vfns;
    const VTableSet &vtbls = getCSVtblsBasedonCHA(cs);
    getVFnsFromVtbls(cs, vtbls, vfns);

    // Cache.
    csCHAMap[cs] = vfns;
    // Return cached object, not the stack object.
    return csCHAMap.at(cs);
}

const VTableSet &DCHGraph::getCSVtblsBasedonCHA(CallSite cs) {
    const llvm::DIType *type = getCanonicalType(getCSStaticType(cs));
    // Check if we've already computed.
    if (vtblCHAMap.find(type) != vtblCHAMap.end()) {
        return vtblCHAMap.at(type);
    }

    VTableSet vtblSet;
    std::set<const DCHNode *> children = cha(type, false);
    for (std::set<const DCHNode *>::const_iterator childI = children.begin(); childI != children.end(); ++childI) {
        const GlobalValue *vtbl = (*childI)->getVTable();
        // TODO: what if it is null?
        if (vtbl != nullptr) {
            vtblSet.insert(vtbl);
        }
    }

    // Cache.
    vtblCHAMap[type] = vtblSet;
    // Return cached version - not the stack object.
    return vtblCHAMap.at(type);
}

void DCHGraph::getVFnsFromVtbls(CallSite cs, const VTableSet &vtbls, VFunSet &virtualFunctions) {
    size_t idx = cppUtil::getVCallIdx(cs);
    std::string funName = cppUtil::getFunNameOfVCallSite(cs);
    for (VTableSet::const_iterator vtblI = vtbls.begin(); vtblI != vtbls.end(); ++vtblI) {
        assert(vtblToTypeMap.find(*vtblI) != vtblToTypeMap.end() && "floating vtbl");
        const llvm::DIType *type = vtblToTypeMap.at(*vtblI);
        assert(hasNode(type) && "trying to get vtbl for type not in graph");
        const DCHNode *node = getNode(type);
        std::vector<std::vector<const Function *>> allVfns = node->getVfnVectors();
        for (std::vector<std::vector<const Function *>>::const_iterator vfnVI = allVfns.begin(); vfnVI != allVfns.end(); ++vfnVI) {
            // We only care about any virtual function corresponding to idx.
            if (idx >= vfnVI->size()) {
                continue;
            }

            const Function *callee = (*vfnVI)[idx];
            // Practically a copy of that in lib/MemoryModel/CHA.cpp
            if (cs.arg_size() == callee->arg_size() || (cs.getFunctionType()->isVarArg() && callee->isVarArg())) {
                cppUtil::DemangledName dname = cppUtil::demangle(callee->getName().str());
                std::string calleeName = dname.funcName;

                /*
                 * The compiler will add some special suffix (e.g.,
                 * "[abi:cxx11]") to the end of some virtual function:
                 * In dealII
                 * function: FE_Q<3>::get_name
                 * will be mangled as: _ZNK4FE_QILi3EE8get_nameB5cxx11Ev
                 * after demangling: FE_Q<3>::get_name[abi:cxx11]
                 * The special suffix ("[abi:cxx11]") needs to be removed
                 */
                const std::string suffix("[abi:cxx11]");
                size_t suffixPos = calleeName.rfind(suffix);
                if (suffixPos != std::string::npos) {
                    calleeName.erase(suffixPos, suffix.size());
                }

                /*
                 * if we can't get the function name of a virtual callsite, all virtual
                 * functions corresponding to idx will be valid
                 */
                if (funName.size() == 0) {
                    virtualFunctions.insert(callee);
                } else if (funName[0] == '~') {
                    /*
                     * if the virtual callsite is calling a destructor, then all
                     * destructors in the ch will be valid
                     * class A { virtual ~A(){} };
                     * class B: public A { virtual ~B(){} };
                     * int main() {
                     *   A *a = new B;
                     *   delete a;  /// the function name of this virtual callsite is ~A()
                     * }
                     */
                    if (calleeName[0] == '~') {
                        virtualFunctions.insert(callee);
                    }
                } else {
                    /*
                     * For other virtual function calls, the function name of the callsite
                     * and the function name of the target callee should match exactly
                     */
                    if (funName.compare(calleeName) == 0) {
                        virtualFunctions.insert(callee);
                    }
                }
            }
        }
    }
}

bool DCHGraph::isBase(const llvm::DIType *a, const llvm::DIType *b, bool firstField) {
    a = getCanonicalType(a);
    b = getCanonicalType(b);
    assert(hasNode(a) && hasNode(b) && "DCHG: isBase query for non-existent node!");
    const DCHNode *bNode = getNode(b);

    std::set<const DCHNode *> &aChildren = cha(a, firstField);
    return aChildren.find(bNode) != aChildren.end();
}

const DIType *DCHGraph::getCanonicalType(const DIType *t) {
    if (canonicalTypeMap.find(t) != canonicalTypeMap.end()) {
        return canonicalTypeMap.at(t);
    }

    // Canonical type for t is not cached, find one for it.
    for (std::set<const DIType *>::iterator canonTypeI = canonicalTypes.begin(); canonTypeI != canonicalTypes.end(); ++canonTypeI) {
        if (teq(t, *canonTypeI)) {
            // Found a canonical type.
            canonicalTypeMap[t] = *canonTypeI;
            return canonicalTypeMap[t];
        }
    }

    // No canonical type found, so t will be a canonical type.
    canonicalTypes.insert(t);
    canonicalTypeMap[t] = t;

    return canonicalTypeMap[t];
}

const DIType *DCHGraph::stripQualifiers(const DIType *t) {
    while (true) {
        // nullptr means void.
        if (t == nullptr
            || SVFUtil::isa<DIBasicType>(t)
            || SVFUtil::isa<DISubroutineType>(t)) {
            break;
        }

        unsigned tag = t->getTag();
        // Verbose for clarity.
        if (   tag == dwarf::DW_TAG_const_type
            || tag == dwarf::DW_TAG_atomic_type
            || tag == dwarf::DW_TAG_volatile_type
            || tag == dwarf::DW_TAG_restrict_type
            || tag == dwarf::DW_TAG_typedef) {
            // Qualifier - get underlying type.
            const DIDerivedType *dt = llvm::dyn_cast<DIDerivedType>(t);
            assert(t && "DCHG: expected DerivedType");
            t = dt->getBaseType();
        } else if (   tag == dwarf::DW_TAG_array_type
                   || tag == dwarf::DW_TAG_class_type
                   || tag == dwarf::DW_TAG_structure_type
                   || tag == dwarf::DW_TAG_union_type
                   || tag == dwarf::DW_TAG_enumeration_type
                   || tag == dwarf::DW_TAG_member
                   || tag == dwarf::DW_TAG_pointer_type
                   || tag == dwarf::DW_TAG_ptr_to_member_type
                   || tag == dwarf::DW_TAG_reference_type
                   || tag == dwarf::DW_TAG_rvalue_reference_type) {
            // TODO: check rvalue_reference_type.
            // TODO: maybe this function should strip members too?
            // Hit a non-qualifier.
            break;
        } else if (   tag == dwarf::DW_TAG_inheritance
                   || tag == dwarf::DW_TAG_friend) {
            assert(false && "DCHG: unexpected tag when stripping qualifiers");
        } else {
            assert(false && "DCHG: unhandled tag when stripping qualifiers");
        }
    }

    return t;
}

bool DCHGraph::teq(const DIType *t1, const DIType *t2) {
    // TODO: semantics for pointer-to-member
    t1 = stripQualifiers(t1);
    t2 = stripQualifiers(t2);

    if (t1 == t2) {
        // Trivial case.
        return true;
    }

    if ((t1 == nullptr && t2 && t2->getName() == ctirInternalUntypedName)
        || (t2 == nullptr && t1 && t1->getName() == ctirInternalUntypedName)) {
        // We're treating the internal untyped from ctir as void (null).
        return true;
    }


    if (t1 == nullptr || t2 == nullptr) {
        // Since t1 != t2 and one of them is null, it is
        // impossible for them to be equal.
        return false;
    }

    // Check if we need base type comparisons.
    if (SVFUtil::isa<DIBasicType>(t1) && SVFUtil::isa<DIBasicType>(t2)) {
        const DIBasicType *b1 = SVFUtil::dyn_cast<DIBasicType>(t1);
        const DIBasicType *b2 = SVFUtil::dyn_cast<DIBasicType>(t2);

        unsigned enc1 = b1->getEncoding();
        unsigned enc2 = b2->getEncoding();
        bool okayEnc = ((enc1 == dwarf::DW_ATE_signed || enc1 == dwarf::DW_ATE_unsigned || enc1 == dwarf::DW_ATE_boolean)
                        && (enc2 == dwarf::DW_ATE_signed || enc2 == dwarf::DW_ATE_unsigned || enc2 == dwarf::DW_ATE_boolean))
                       ||
                       (enc1 == dwarf::DW_ATE_float && enc2 == dwarf::DW_ATE_float)
                       ||
                       ((enc1 == dwarf::DW_ATE_signed_char || enc1 == dwarf::DW_ATE_unsigned_char)
                        &&
                        (enc2 == dwarf::DW_ATE_signed_char || enc2 == dwarf::DW_ATE_unsigned_char));

        if (!okayEnc) return false;
        // Now we have split integers, floats, and chars, ignoring signedness.

        return t1->getSizeInBits() == t2->getSizeInBits()
               && t1->getAlignInBits() == t2->getAlignInBits();
    }

    // Check, do we need to compare base types?
    // This makes pointers, references, and arrays equivalent.
    // Will handle member types.
    if ((SVFUtil::isa<DIDerivedType>(t1) || t1->getTag() == dwarf::DW_TAG_array_type)
        && (SVFUtil::isa<DIDerivedType>(t2) || t2->getTag() == dwarf::DW_TAG_array_type)) {
        const DIType *base1, *base2;

        // Set base1.
        if (const DIDerivedType *d1 = SVFUtil::dyn_cast<DIDerivedType>(t1)) {
            base1 = d1->getBaseType();
        } else {
            const DICompositeType *c1 = SVFUtil::dyn_cast<DICompositeType>(t1);
            assert(c1 && "teq: bad cast for array type");
            base1 = c1->getBaseType();
        }

        // Set base2.
        if (const DIDerivedType *d2 = SVFUtil::dyn_cast<DIDerivedType>(t2)) {
            base2 = d2->getBaseType();
        } else {
            const DICompositeType *c2 = SVFUtil::dyn_cast<DICompositeType>(t2);
            assert(c2 && "teq: bad cast for array type");
            base2 = c2->getBaseType();
        }

        return teq(base1, base2);
    }

    if (SVFUtil::isa<DICompositeType>(t1) && SVFUtil::isa<DICompositeType>(t2)) {
        const DICompositeType *ct1 = SVFUtil::dyn_cast<DICompositeType>(t1);
        const DICompositeType *ct2 = SVFUtil::dyn_cast<DICompositeType>(t2);

        if (ct1->getTag() != ct2->getTag()) return false;

        // TODO: too coarse?
        // Treat all enums the same for now.
        if (ct1->getTag() == dwarf::DW_TAG_enumeration_type) {
            return true;
        }

        // C++ classes? Check mangled name.
        if (ct1->getTag() == dwarf::DW_TAG_class_type) {
            return ct1->getIdentifier() == ct2->getIdentifier();
        }

        // Either union or struct, simply test all fields are equal.
        // Seems like it is enough to check it was defined in the same place.
        // The elements sometimes differ but are referring to the same fields.
        return    ct1->getName() == ct2->getName()
               && ct1->getFile() == ct2->getFile()
               && ct1->getLine() == ct2->getLine();
    }

    // TODO: do we need special handling of subroutine types?

    // They were not equal base types (discounting signedness), nor were they
    // "equal" pointers/references/arrays, nor were they the structurally equivalent,
    // nor were they completely equal.
    return false;
}

const DIType *DCHGraph::getTypeFromCTirMetadata(const Value *v) {
    assert(v != nullptr && "DCHG: trying to get metadata from nullptr!");

    const MDNode *mdNode = nullptr;
    if (const Instruction *inst = SVFUtil::dyn_cast<Instruction>(v)) {
        mdNode = inst->getMetadata(SVFModule::ctirMetadataName);
    } else if (const GlobalObject *go = SVFUtil::dyn_cast<GlobalObject>(v)) {
        mdNode = go->getMetadata(SVFModule::ctirMetadataName);
    }

    if (mdNode == nullptr) {
        return nullptr;
    }

    const DIType *type = SVFUtil::dyn_cast<DIType>(mdNode);
    if (type == nullptr) {
        llvm::errs() << "FSTF: bad ctir metadata type\n";
        return nullptr;
    }

    return getCanonicalType(type);
}


std::string DCHGraph::diTypeToStr(const DIType *t) {
    std::stringstream ss;

    if (t == nullptr) {
        return "null (void)";
    }

    if (const DIBasicType *bt = SVFUtil::dyn_cast<DIBasicType>(t)) {
        ss << std::string(bt->getName());
    } else if (const DIDerivedType *dt = SVFUtil::dyn_cast<DIDerivedType>(t)) {
        if (dt->getName() == "__vtbl_ptr_type") {
            ss << "(vtbl * =) __vtbl_ptr_type";
        } else if (dt->getTag() == dwarf::DW_TAG_const_type) {
            ss << "const " << diTypeToStr(dt->getBaseType());
        } else if (dt->getTag() == dwarf::DW_TAG_volatile_type) {
            ss << "volatile " << diTypeToStr(dt->getBaseType());
        } else if (dt->getTag() == dwarf::DW_TAG_restrict_type) {
            ss << "restrict " << diTypeToStr(dt->getBaseType());
        } else if (dt->getTag() == dwarf::DW_TAG_atomic_type) {
            ss << "atomic " << diTypeToStr(dt->getBaseType());
        } else if (dt->getTag() == dwarf::DW_TAG_pointer_type) {
            ss << diTypeToStr(dt->getBaseType()) << " *";
        } else if (dt->getTag() == dwarf::DW_TAG_ptr_to_member_type) {
            // TODO: double check
            ss << diTypeToStr(dt->getBaseType()) << " *";
        } else if (dt->getTag() == dwarf::DW_TAG_reference_type) {
            ss << diTypeToStr(dt->getBaseType()) << " &";
        } else if (dt->getTag() == dwarf::DW_TAG_rvalue_reference_type) {
            // TODO: double check
            ss << diTypeToStr(dt->getBaseType()) << " &";
        } else if (dt->getTag() == dwarf::DW_TAG_typedef) {
            ss << std::string(dt->getName()) << "->" << diTypeToStr(dt->getBaseType());
        }
    } else if (const DICompositeType *ct = SVFUtil::dyn_cast<DICompositeType>(t)) {
        if (ct->getTag() == dwarf::DW_TAG_class_type
            || ct->getTag() == dwarf::DW_TAG_structure_type
            || ct->getTag() == dwarf::DW_TAG_union_type) {

            if (ct->getTag() == dwarf::DW_TAG_class_type) {
                ss << "class";
            } else if (ct->getTag() == dwarf::DW_TAG_structure_type) {
                ss << "struct";
            } else if (ct->getTag() == dwarf::DW_TAG_union_type) {
                ss << "union";
            }

            ss << ".";

            if (ct->getName() != "") {
                ss << std::string(ct->getName());
            } else {
                // Iterate over the element types.
                ss << "{ ";

                llvm::DINodeArray fields = ct->getElements();
                for (unsigned i = 0; i < fields.size(); ++i) {
                    // fields[i] gives a type which is DW_TAG_member, we want the member's type (getBaseType).
                    // It can also give a Subprogram type if the class just had non-virtual functions.
                    if (const DISubprogram *sp = SVFUtil::dyn_cast<DISubprogram>(fields[i])) {
                        ss << std::string(sp->getName());
                    } else if (const DIDerivedType *mt = SVFUtil::dyn_cast<DIDerivedType>(fields[i])) {
                        assert(mt->getTag() == dwarf::DW_TAG_member && "DCHG: expected member");
                    }

                    if (i != fields.size() - 1) {
                        ss << ", ";
                    }
                }

                ss << " }";
            }
        } else if (ct->getTag() == dwarf::DW_TAG_array_type) {
            ss << diTypeToStr(ct->getBaseType());
            llvm::DINodeArray sizes = ct->getElements();
            for (unsigned i = 0; i < sizes.size(); ++i) {
                llvm::DISubrange *sr = SVFUtil::dyn_cast<llvm::DISubrange>(sizes[0]);
                assert(sr != nullptr && "DCHG: non-subrange as array element?");
                // TODO: use sr->getCount
                ss << "[" << 123 << "]";
            }
        } else if (ct->getTag() == dwarf::DW_TAG_enumeration_type) {
            ss << "enum " << diTypeToStr(ct->getBaseType());
        } else if (ct->getTag() == dwarf::DW_TAG_union_type) {

        }
    } else if (const DISubroutineType *st = SVFUtil::dyn_cast<DISubroutineType>(t)) {
        ss << std::string(st->getName());
    }

    return ss.str();
}

static std::string indent(size_t n) {
    return std::string(n, ' ');
}

void DCHGraph::print(void) const {
    static const std::string line = "-------------------------------------\n";
    static const std::string thickLine = "=====================================\n";
    static const size_t singleIndent = 2;

    size_t currIndent = 0;
    llvm::outs() << thickLine;
    for (DCHGraph::const_iterator it = begin(); it != end(); ++it) {
        const NodeID id = it->first;
        const DCHNode *node = it->second;

        // TODO: need to properly name non-class nodes.
        llvm::outs() << indent(currIndent) << id << ": " << diTypeToStr(node->getType()) << " [" << node->getType() << "]" << "\n";

        currIndent += singleIndent;
        llvm::outs() << indent(currIndent) << "Virtual functions\n";
        currIndent += singleIndent;
        const std::vector<std::vector<const Function *>> &vfnVectors = node->getVfnVectors();
        for (unsigned i = 0; i < vfnVectors.size(); ++i) {
            llvm::outs() << indent(currIndent) << "[ vtable #" << i << " ]\n";
            currIndent += singleIndent;
            for (unsigned j = 0; j < vfnVectors[i].size(); ++j) {
                struct cppUtil::DemangledName dname = cppUtil::demangle(vfnVectors[i][j]->getName().str());
                llvm::outs() << indent(currIndent) << "[" << j << "] "
                             << dname.className << "::" << dname.funcName << "\n";
            }

            currIndent -= singleIndent;
        }

        // Nothing was printed.
        if (vfnVectors.size() == 0) {
            llvm::outs() << indent(currIndent) << "(none)\n";
        }

        currIndent -= singleIndent;

        llvm::outs() << indent(currIndent) << "Bases\n";
        currIndent += singleIndent;
        for (DCHEdge::DCHEdgeSetTy::const_iterator edgeI = node->OutEdgeBegin(); edgeI != node->OutEdgeEnd(); ++edgeI) {
            const DCHEdge *edge = *edgeI;
            std::string arrow;
            if (edge->getEdgeKind() == DCHEdge::INHERITANCE) {
                arrow = "--inheritance-->";
            } else if (edge->getEdgeKind() == DCHEdge::FIRST_FIELD) {
                arrow = "--first-field-->";
            } else if (edge->getEdgeKind() == DCHEdge::INSTANCE) {
                arrow = "---instance---->";
            } else {
                arrow = "----unknown---->";
            }

            llvm::outs() << indent(currIndent) << "[ " << diTypeToStr(node->getType()) << " ] "
                         << arrow << " [ " << diTypeToStr(edge->getDstNode()->getType()) << " ]\n";
        }

        if (node->getOutEdges().size() == 0) {
            llvm::outs() << indent(currIndent) << "(none)\n";
        }

        currIndent -= singleIndent;

        llvm::outs() << indent(currIndent) << "Typedefs\n";

        currIndent += singleIndent;

        const std::set<const llvm::DIDerivedType *> &typedefs = node->getTypedefs();
        for (std::set<const llvm::DIDerivedType *>::iterator typedefI = typedefs.begin(); typedefI != typedefs.end(); ++typedefI) {
            const llvm::DIDerivedType *typedefType = *typedefI;
            std::string typedefName = "void";
            if (typedefType != NULL) {
                typedefName = typedefType->getName();
            }

            llvm::outs() << indent(currIndent) << typedefName << "\n";
        }

        if (typedefs.size() == 0) {
            llvm::outs() << indent(currIndent) << "(none)\n";
        }

        currIndent -= singleIndent;

        currIndent -= singleIndent;

        if (std::next(it) != end()) {
            llvm::outs() << line;
        }
    }

    llvm::outs() << thickLine;
    llvm::outs() << "Constructor to type mapping\n";
    llvm::outs() << line;
    for (std::map<const Function *, const DIType *>::const_iterator ctI = constructorToType.begin(); ctI != constructorToType.end(); ++ctI) {
        cppUtil::DemangledName dname = cppUtil::demangle(ctI->first->getName().str());
        llvm::outs() << dname.className << "::" << dname.funcName << " : " << diTypeToStr(ctI->second) << "\n";
    }

    llvm::outs() << thickLine;
}

