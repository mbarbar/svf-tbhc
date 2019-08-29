//===----- DCHG.cpp  CHG using DWARF debug info ------------------------//
//
/*
 * DCHG.cpp
 *
 *  Created on: Aug 24, 2019
 *      Author: Mohamad Barbar
 */

#include "MemoryModel/DCHG.h"

void DCHGraph::buildCHG(void) {
    llvm::DebugInfoFinder finder;
    finder.processModule(module);

    for (llvm::DebugInfoFinder::type_iterator diTypeI = finder.types().begin(); diTypeI != finder.types().end(); ++diTypeI) {
        llvm::DIType *type = *diTypeI;

        if (llvm::DIBasicType *basicType = SVFUtil::dyn_cast<llvm::DIBasicType>(type)) {
            // TODO: add node likely.
        } else if (llvm::DICompositeType *compositeType = SVFUtil::dyn_cast<llvm::DICompositeType>(type)) {
            switch (compositeType->getTag()) {
            case llvm::dwarf::DW_TAG_array_type:
                // TODO: add node likely 
                break;
            case llvm::dwarf::DW_TAG_class_type:
                // TODO: add node.
                break;
            case llvm::dwarf::DW_TAG_structure_type:
                // TODO: unsure.
                break;
            case llvm::dwarf::DW_TAG_union_type:
                // TODO: unsure.
                break;
            case llvm::dwarf::DW_TAG_enumeration_type:
                break;
            default:
                assert(false && "DCHGraph::buildCHG: unexpected CompositeType tag.");
            }
        } else if (llvm::DIDerivedType *derivedType = SVFUtil::dyn_cast<llvm::DIDerivedType>(type)) {
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
        } else if (llvm::DISubroutineType *subroutineType = SVFUtil::dyn_cast<llvm::DISubroutineType>(type)) {
            // TODO: unsure.
        } else {
            assert(false && "DCHGraph::buildCHG: unexpected DIType.");
        }
    }

    // TODO: first field edges.
}

