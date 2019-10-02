//===----- DCHG.cpp  CHG using DWARF debug info ------------------------//
//
/*
 * DCHG.cpp
 *
 *  Created on: Aug 24, 2019
 *      Author: Mohamad Barbar
 */

#include "MemoryModel/DCHG.h"
#include "llvm/IR/DebugInfo.h"

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
                llvm::DIDerivedType *firstMember = SVFUtil::dyn_cast<llvm::DIDerivedType>(fields[0]);
                assert(firstMember != NULL && "DCHGraph::handleDICompositeType: first field is not a DIDerivedType?");
                addEdge(compositeType, firstMember->getBaseType(), DCHEdge::FIRST_FIELD);
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
        DCHEdge *edge = addEdge(derivedType->getBaseType(), SVFUtil::dyn_cast<llvm::DIType>(derivedType->getScope()), DCHEdge::INHERITANCE);
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

void DCHGraph::handleTypedef(const llvm::DIDerivedType *typedefType) {
    assert(typedefType && typedefType->getTag() == llvm::dwarf::DW_TAG_typedef);

    // Need to gather them in a set first because we don't know the base type till
    // we get to the bottom of the (potentially many) typedefs.
    std::vector<const llvm::DIDerivedType *> typedefs;
    // Check for NULL because you can typedef void.
    while (typedefType != NULL && typedefType->getTag() == llvm::dwarf::DW_TAG_typedef) {
        typedefs.push_back(typedefType);
        if (!SVFUtil::isa<llvm::DIDerivedType>(typedefType->getBaseType())) {
            break;
        }

        typedefType = SVFUtil::dyn_cast<llvm::DIDerivedType>(typedefType->getBaseType());
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

void DCHGraph::buildVTables(void) {
    llvm::DebugInfoFinder finder;
    for (u32_t i = 0; i < svfModule.getModuleNum(); ++i) {
        finder.processModule(*(svfModule.getModule(i)));
    }

    for (llvm::DebugInfoFinder::iterator diSubProgI = finder.subprogram_begin(); diSubProgI != finder.subprogram_end(); ++diSubProgI) {
        llvm::DISubrogram *diSubProg = *diSubProgI;
        if (!(diSubProg->getSPFlags() & llvm::DISubrogram::SPFlagVirtuality)) {
            // Don't care about non-virtuals.
            continue;
        }

        llvm::DIType *type = diSubProg->getContainingType();
        const llvm::Function *vFun = svfModule.getFunction(diSubProg->getLinkageName());
        getOrCreateNode(type)->addVirtualFunction(vFun, diSubProg->getVirtualIndex());
    }

    // We now have the primary vtables in place (with gaps for inherited functions).
    // We need to handle inheritance, particularly multiple inheritance.
}

DCHNode *DCHGraph::getOrCreateNode(const llvm::DIType *type) {
    // TODO: this fails for `void`.
    assert(type != NULL && "DCHGraph::getOrCreateNode: type is null.");

    // Check, does the node for type exist?
    if (diTypeToNodeMap[type] != NULL) {
        return diTypeToNodeMap[type];
    }

    if (typedefToNodeMap[type] != NULL) {
        return typedefToNodeMap[type];
    }

    DCHNode *node = new DCHNode(type);
    addGNode(node->getId(), node);
    llvm::outs() << type << " : " << type->getName() << "\n";
    diTypeToNodeMap[type] = node;
    // TODO: name map, necessary?

    // TODO: handle templates.

    return node;
}

DCHEdge *DCHGraph::addEdge(const llvm::DIType *t1, const llvm::DIType *t2, DCHEdge::DCHEDGETYPE et) {
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

DCHEdge *DCHGraph::hasEdge(const llvm::DIType *t1, const llvm::DIType *t2, DCHEdge::DCHEDGETYPE et) {
    DCHNode *src = getOrCreateNode(t1);
    DCHNode *dst = getOrCreateNode(t2);

    for (DCHEdge::DCHEdgeSetTy::const_iterator edgeI = src->getOutEdges().begin(); edgeI != src->getOutEdges().end(); ++edgeI) {
        DCHNode *node = (*edgeI)->getDstNode();
        DCHEdge::DCHEDGETYPE edgeType = (*edgeI)->getEdgeType();
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

    buildVTables();
}

