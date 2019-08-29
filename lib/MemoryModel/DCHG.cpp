//===----- DCHG.cpp  CHG using DWARF debug info ------------------------//
//
/*
 * DCHG.cpp
 *
 *  Created on: Aug 24, 2019
 *      Author: Mohamad Barbar
 */

#include "MemoryModel/DCHG.h"

void DCHGraph::handleDIBasicType(const DIBasicType *basicType) {
    getOrCreateNode(basicType, basicType->getName());
}

void DCHGraph::handleDICompositeType(const DICompositeType *compositeType) {
    switch (compositeType->getTag()) {
    case llvm::dwarf::DW_TAG_array_type:
        // TODO: add node likely 
        break;
    case llvm::dwarf::DW_TAG_class_type:
    case llvm::dwarf::DW_TAG_structure_type:
        getOrCreateNode(compositeType, compositeType->getName());
        break;
    case llvm::dwarf::DW_TAG_union_type:
        // TODO: unsure.
        break;
    case llvm::dwarf::DW_TAG_enumeration_type:
        break;
    default:
        assert(false && "DCHGraph::buildCHG: unexpected CompositeType tag.");
    }
}

void DCHGraph::handleDIDerivedType(const DIDerivedType *derivedType) {
    switch (derivedType->getTag()) {
    case llvm::dwarf::DW_TAG_inheritance:
        // TODO: add inheritance edge.
        break;
    case llvm::dwarf::DW_TAG_member:
        // TODO: don't care it seems.
        break;
    case llvm::dwarf::DW_TAG_typedef:
        // TODO: perhaps attach it to the appropriate node.
        break;
    case llvm::dwarf::DW_TAG_pointer_type:
        // TODO: add node likely.
        break;
    case llvm::dwarf::DW_TAG_ptr_to_member_type:
        // TODO: add node likely 
        break;
    case llvm::dwarf::DW_TAG_reference_type:
        // TODO: unsure.
        break;
    case llvm::dwarf::DW_TAG_const_type:
        // TODO: unsure
        break;
    case llvm::dwarf::DW_TAG_atomic_type:
        // TODO: unsure
        break;
    case llvm::dwarf::DW_TAG_volatile_type:
        // TODO: unsure
        break;
    case llvm::dwarf::DW_TAG_restrict_type:
        // TODO: unsure
        break;
    case llvm::dwarf::DW_TAG_friend:
        // TODO: unsure.
        break;
    default:
        assert(false && "DCHGraph::buildCHG: unexpected DerivedType tag.");
    }
}

void DCHGraph::handleDISubroutineType(const DISubroutineType *subroutineType) {
}

DCHNode *DCHGraph::getOrCreateNode(llvm::DIType *type, std::string name) {
    assert(type != NULL && "DCHGraph::getOrCreateNode: type is null.");

    // Check, does the node for type exist?
    if (diTypeToNodeMap[type] != NULL) {
        return diTypeToNodeMap[type];
    }

    DCHNode *node = new DCHNode(type, name);
    diTypeToNodeMap[type] = node;
    // TODO: name map, necessary?

    return node;
}

void DCHGraph::buildCHG(void) {
    llvm::DebugInfoFinder finder;
    finder.processModule(module);

    for (llvm::DebugInfoFinder::type_iterator diTypeI = finder.types().begin(); diTypeI != finder.types().end(); ++diTypeI) {
        llvm::DIType *type = *diTypeI;

        if (llvm::DIBasicType *basicType = SVFUtil::dyn_cast<llvm::DIBasicType>(type)) {
            handleDIBasicType(basicType);
        } else if (llvm::DICompositeType *compositeType = SVFUtil::dyn_cast<llvm::DICompositeType>(type)) {
            handleDICompositeType(compositeType);
        } else if (llvm::DIDerivedType *derivedType = SVFUtil::dyn_cast<llvm::DIDerivedType>(type)) {
            handleDIDerivedType(derivedType);
        } else if (llvm::DISubroutineType *subroutineType = SVFUtil::dyn_cast<llvm::DISubroutineType>(type)) {
            handleDIDerivedType(subroutineType);
        } else {
            assert(false && "DCHGraph::buildCHG: unexpected DIType.");
        }
    }

    // TODO: first field edges.
}

