//===----- DCHG.cpp  CHG using DWARF debug info ------------------------//
//
/*
 * DCHG.cpp
 *
 *  Created on: Aug 24, 2019
 *      Author: Mohamad Barbar
 */

#include "MemoryModel/DCHG.h"
#include "Util/CPPUtil.h"

#include "llvm/IR/DebugInfo.h"

const std::string DCHGraph::tirMetadataName = "tir";

void DCHGraph::handleDIBasicType(const llvm::DIBasicType *basicType) {
    getOrCreateNode(basicType);
}

void DCHGraph::handleDICompositeType(const llvm::DICompositeType *compositeType) {
    switch (compositeType->getTag()) {
    case llvm::dwarf::DW_TAG_array_type:
        if (extended) getOrCreateNode(compositeType);
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

        break;
    case llvm::dwarf::DW_TAG_union_type:
        // TODO: unsure.
        break;
    case llvm::dwarf::DW_TAG_enumeration_type:
        // TODO: unsure.
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
        // TODO: are these just pointers?
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
        // Want to quickly get the base type from the typedef.
        typedefToNodeMap[*typedefI] = baseTypeNode;
    }
}

void DCHGraph::buildVTables(const Module &module) {
    for (Module::const_global_iterator gvI = module.global_begin(); gvI != module.global_end(); ++gvI) {
        // Though this will return more than GlobalVariables, we only care about GlobalVariables (for the vtbls).
        const GlobalVariable *gv = SVFUtil::dyn_cast<const GlobalVariable>(&(*gvI));
        if (gv == nullptr) continue;
        if (gv->hasMetadata("tirvt") && gv->getNumOperands() > 0) {
            llvm::DIType *type = llvm::dyn_cast<llvm::DIType>(gv->getMetadata("tirvt"));
            assert(type && "Bad metadata for tirvt");
            DCHNode *node = getOrCreateNode(type);
            node->setVTable(gv);

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

DCHNode *DCHGraph::getOrCreateNode(const llvm::DIType *type) {
    // Check, does the node for type exist?
    if (diTypeToNodeMap[type] != NULL) {
        return diTypeToNodeMap[type];
    }

    if (typedefToNodeMap[type] != NULL) {
        return typedefToNodeMap[type];
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

    for (llvm::DebugInfoFinder::type_iterator diTypeI = finder.types().begin(); diTypeI != finder.types().end(); ++diTypeI) {
        llvm::DIType *type = *diTypeI;

        if (llvm::DIBasicType *basicType = SVFUtil::dyn_cast<llvm::DIBasicType>(type)) {
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
}

const VFunSet &getCSVFsBasedonCHA(CallSite cs) const {
    VFunSet vfnSet;
    return vfnSet;
}

const VTableSet &getCSVtblsBasedonCHA(CallSite cs) const {
    VTableSet vtblSet;
    return vtblSet;
}

void getVFnsFromVtbls(CallSite cs, VTableSet &vtbls, VFunSet &overrideFunctions) const {
}


static std::string indent(size_t n) {
    return std::string(n, ' ');
}

void DCHGraph::print(void) const {
    static const std::string line = "-------------------------------------\n";
    static const size_t singleIndent = 2;

    size_t currIndent = 0;
    for (DCHGraph::const_iterator it = begin(); it != end(); ++it) {
        const NodeID id = it->first;
        const DCHNode *node = it->second;

        llvm::outs() << line;

        // TODO: need to properly name non-class nodes.
        llvm::outs() << indent(currIndent) << id << ": " << node->getName() << "\n";

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

            llvm::outs() << indent(currIndent) << "[ " << node->getName() << " ] "
                         << arrow << " [ " << edge->getDstNode()->getName() << " ]\n";
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
    }
}

